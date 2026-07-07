#include "viewer/app/glfieldrenderer.h"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>

namespace met::app {
namespace {

constexpr unsigned int kGL_CLAMP_TO_EDGE = 0x812F;
constexpr unsigned int kGL_UNPACK_ROW_LENGTH = 0x0CF2;
constexpr unsigned int kGL_UNPACK_SKIP_ROWS = 0x0CF3;
constexpr unsigned int kGL_UNPACK_SKIP_PIXELS = 0x0CF4;

const char* kVertexSrc = R"(#version 330
in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 330
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uField;   // colormapped RGBA8 in grid order
uniform vec2 uViewport;
uniform vec2 uTopLeft;      // world-pixel coords of the top-left corner
uniform float uWorldSize;   // 256 * 2^zoom
uniform vec4 uGrid0;        // lon0, lat0, dlon, dlat
uniform vec2 uGridN;        // nlon, nlat
uniform int uGlobalWrap;
uniform float uOpacity;
const float PI = 3.141592653589793;
void main() {
    // vUv.y runs bottom->top in GL; screen y runs top->down.
    float px = vUv.x * uViewport.x;
    float py = (1.0 - vUv.y) * uViewport.y;
    float wx = uTopLeft.x + px;
    float wy = uTopLeft.y + py;
    float lon = wx / uWorldSize * 360.0 - 180.0;
    float n = PI * (1.0 - 2.0 * wy / uWorldSize);
    float lat = atan(sinh(n)) * 180.0 / PI;

    float delta = mod(lon - uGrid0.x + 180.0, 360.0) - 180.0;
    float fx = delta / uGrid0.z;
    float fy = (lat - uGrid0.y) / uGrid0.w;
    bool inx = (uGlobalWrap == 1) ? true : (fx >= 0.0 && fx <= uGridN.x - 1.0);
    bool iny = fy >= 0.0 && fy <= uGridN.y - 1.0;
    if (uGlobalWrap == 1) fx = mod(mod(fx, uGridN.x) + uGridN.x, uGridN.x);

    // Out-of-domain / missing fragments emit transparent and blend to nothing
    // (revealing the tiles). Avoid `discard`, which corrupts adjacent fragments
    // in the 2x2 quad on some drivers. textureLod avoids derivative computation.
    vec4 outc = vec4(0.0);
    if (inx && iny) {
        float cx = clamp(fx, 0.0, uGridN.x - 1.0);
        float cy = clamp(fy, 0.0, uGridN.y - 1.0);
        vec2 uv = vec2((cx + 0.5) / uGridN.x, (cy + 0.5) / uGridN.y);
        vec4 c = textureLod(uField, uv, 0.0);
        if (c.a >= 0.5) outc = vec4(c.rgb, uOpacity * c.a);
    }
    fragColor = outc;
}
)";

}  // namespace

GlFieldRenderer::GlFieldRenderer() = default;

GlFieldRenderer::~GlFieldRenderer() {
    if (fieldTex_) glDeleteTextures(1, &fieldTex_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool GlFieldRenderer::init() {
    initializeOpenGLFunctions();

    program_ = std::make_unique<QOpenGLShaderProgram>();
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSrc) ||
        !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentSrc) ||
        !program_->link()) {
        program_.reset();
        return false;
    }

    vao_.create();  // required in a core/forward-compatible context

    // A single oversized triangle covering the viewport (no internal diagonal,
    // avoiding triangle-strip interpolation artifacts).
    static const float verts[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &fieldTex_);
    ready_ = true;
    return true;
}

void GlFieldRenderer::uploadField(int nlon, int nlat, const unsigned char* rgba) {
    if (!ready_) return;
    glBindTexture(GL_TEXTURE_2D, fieldTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(kGL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(kGL_CLAMP_TO_EDGE));
    // QPainter's GL paint engine may leave pixel-store state set (row length,
    // skips); reset it so the texture is read with the expected tight layout.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(static_cast<GLenum>(kGL_UNPACK_ROW_LENGTH), 0);
    glPixelStorei(static_cast<GLenum>(kGL_UNPACK_SKIP_ROWS), 0);
    glPixelStorei(static_cast<GLenum>(kGL_UNPACK_SKIP_PIXELS), 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nlon, nlat, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    haveField_ = true;
}

void GlFieldRenderer::render(const Grid& grid, const View& view, float opacity) {
    if (!ready_ || !haveField_ || !program_) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    program_->bind();
    program_->setUniformValue(
        "uViewport", QVector2D(static_cast<float>(view.widthPx), static_cast<float>(view.heightPx)));
    program_->setUniformValue("uTopLeft",
                              QVector2D(static_cast<float>(view.topLeftWorldX),
                                        static_cast<float>(view.topLeftWorldY)));
    program_->setUniformValue("uWorldSize", static_cast<float>(view.worldSize));
    program_->setUniformValue("uGrid0", QVector4D(grid.lon0, grid.lat0, grid.dlon, grid.dlat));
    program_->setUniformValue("uGridN", QVector2D(static_cast<float>(grid.nlon),
                                                  static_cast<float>(grid.nlat)));
    program_->setUniformValue("uGlobalWrap", grid.globalWrap ? 1 : 0);
    program_->setUniformValue("uOpacity", opacity);

    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);  // clear any sampler object QPainter left on this unit
    glBindTexture(GL_TEXTURE_2D, fieldTex_);
    program_->setUniformValue("uField", 0);

    vao_.bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const int loc = program_->attributeLocation("aPos");
    program_->enableAttributeArray(loc);
    program_->setAttributeBuffer(loc, GL_FLOAT, 0, 2);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    program_->disableAttributeArray(loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vao_.release();
    program_->release();
}

}  // namespace met::app
