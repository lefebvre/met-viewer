#include "viewer/app/glfieldrenderer.h"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>

namespace met::app {
namespace {

constexpr unsigned int kGL_CLAMP_TO_EDGE = 0x812F;
constexpr unsigned int kGL_REPEAT = 0x2901;
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
uniform vec2 uTopLeftFrac;  // top-left corner / worldSize, in [0,1)
uniform vec2 uViewportFrac; // viewport size / worldSize
uniform vec4 uGrid0;        // lon0, lat0, dlon, dlat
uniform vec2 uGridN;        // nlon, nlat
uniform int uGlobalWrap;
uniform float uOpacity;
const float PI = 3.141592653589793;
void main() {
    // vUv.y runs bottom->top in GL; screen y runs top->down. Everything is kept in
    // fractions of the world size (computed in double on the CPU) so precision
    // holds at high zoom, where 256*2^z overflows the 24-bit float mantissa.
    float fxw = uTopLeftFrac.x + vUv.x * uViewportFrac.x;
    float fyw = uTopLeftFrac.y + (1.0 - vUv.y) * uViewportFrac.y;
    float lon = fxw * 360.0 - 180.0;
    float n = PI * (1.0 - 2.0 * fyw);
    float lat = atan(sinh(n)) * 180.0 / PI;

    float delta = mod(lon - uGrid0.x + 180.0, 360.0) - 180.0;
    float fx = uGrid0.z != 0.0 ? delta / uGrid0.z : -1.0;      // guard dlon == 0
    float fy = uGrid0.w != 0.0 ? (lat - uGrid0.y) / uGrid0.w : -1.0;  // guard dlat == 0
    bool inx = (uGlobalWrap == 1) ? true : (fx >= 0.0 && fx <= uGridN.x - 1.0);
    bool iny = fy >= 0.0 && fy <= uGridN.y - 1.0;
    if (uGlobalWrap == 1) fx = mod(mod(fx, uGridN.x) + uGridN.x, uGridN.x);

    // Out-of-domain / missing fragments emit transparent and blend to nothing
    // (revealing the tiles). Avoid `discard`, which corrupts adjacent fragments
    // in the 2x2 quad on some drivers. textureLod avoids derivative computation.
    vec4 outc = vec4(0.0);
    if (inx && iny) {
        float cy = clamp(fy, 0.0, uGridN.y - 1.0);
        // Global-wrap grids sample with WRAP_S=REPEAT so the last column blends
        // into column 0 across the ±180° seam; bounded grids clamp.
        float u = (uGlobalWrap == 1) ? (fx + 0.5) / uGridN.x
                                     : (clamp(fx, 0.0, uGridN.x - 1.0) + 0.5) / uGridN.x;
        vec2 uv = vec2(u, (cy + 0.5) / uGridN.y);
        vec4 c = textureLod(uField, uv, 0.0);
        if (c.a >= 0.5) outc = vec4(c.rgb, uOpacity * c.a);
    }
    fragColor = outc;
}
)";

}  // namespace

GlFieldRenderer::GlFieldRenderer() = default;

GlFieldRenderer::~GlFieldRenderer() {
    // Best-effort: no-op if teardown() already ran under a current context.
    if (fieldTex_) glDeleteTextures(1, &fieldTex_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void GlFieldRenderer::teardown() {
    if (fieldTex_) { glDeleteTextures(1, &fieldTex_); fieldTex_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_.isCreated()) vao_.destroy();
    program_.reset();
    ready_ = false;
    haveField_ = false;
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
    // Reject a grid that won't fit in one texture; the caller then uses the CPU warp.
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    if (nlon <= 0 || nlat <= 0 || nlon > maxTex || nlat > maxTex) {
        haveField_ = false;
        return;
    }
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
    const bool ok = glGetError() == GL_NO_ERROR;
    glBindTexture(GL_TEXTURE_2D, 0);
    haveField_ = ok;
}

void GlFieldRenderer::render(const Grid& grid, const View& view, float opacity) {
    if (!ready_ || !haveField_ || !program_) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    program_->bind();
    // Pass world coordinates as fractions of the world size, divided in double on
    // the CPU, so the shader never handles the large (256*2^zoom) magnitudes that
    // lose precision as 32-bit floats at high zoom.
    const double ws = view.worldSize > 0.0 ? view.worldSize : 1.0;
    program_->setUniformValue("uTopLeftFrac",
                              QVector2D(static_cast<float>(view.topLeftWorldX / ws),
                                        static_cast<float>(view.topLeftWorldY / ws)));
    program_->setUniformValue("uViewportFrac",
                              QVector2D(static_cast<float>(view.widthPx / ws),
                                        static_cast<float>(view.heightPx / ws)));
    program_->setUniformValue("uGrid0", QVector4D(grid.lon0, grid.lat0, grid.dlon, grid.dlat));
    program_->setUniformValue("uGridN", QVector2D(static_cast<float>(grid.nlon),
                                                  static_cast<float>(grid.nlat)));
    program_->setUniformValue("uGlobalWrap", grid.globalWrap ? 1 : 0);
    program_->setUniformValue("uOpacity", opacity);

    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);  // clear any sampler object QPainter left on this unit
    glBindTexture(GL_TEXTURE_2D, fieldTex_);
    // Global-wrap grids repeat on S so the antimeridian seam interpolates to
    // column 0 instead of clamping to a one-cell edge.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    grid.globalWrap ? static_cast<GLint>(kGL_REPEAT)
                                    : static_cast<GLint>(kGL_CLAMP_TO_EDGE));
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
