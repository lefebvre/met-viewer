#pragma once

#include <memory>

#include <QOpenGLExtraFunctions>
#include <QOpenGLVertexArrayObject>

class QOpenGLShaderProgram;

namespace met::app {

// GPU field rasterizer. The caller uploads the field already colormapped to a
// small RGBA8 texture in grid order (missing cells transparent); the fragment
// shader does the expensive part on the GPU: for every output pixel it inverts
// the Web Mercator projection, indexes the grid, samples the texture (hardware
// bilinear), and applies the layer opacity. RGBA8 avoids the driver-specific
// pitfalls of float textures. Used only for RegularLatLonGrid fields; the caller
// falls back to the CPU warp otherwise. Must be used within a live QOpenGLWidget
// context.
class GlFieldRenderer : protected QOpenGLExtraFunctions {
public:
    GlFieldRenderer();
    ~GlFieldRenderer();

    bool init();
    [[nodiscard]] bool ready() const { return ready_; }

    // True once a field texture is successfully uploaded. False if the last
    // upload was rejected (e.g. the grid exceeds GL_MAX_TEXTURE_SIZE), so the
    // caller can fall back to the CPU warp.
    [[nodiscard]] bool haveField() const { return haveField_; }

    // Release all GL objects. Must be called with a current context (e.g. from
    // the owning widget's destructor after makeCurrent()).
    void teardown();

    // Upload the colormapped field (RGBA8, nlon x nlat, row-major grid order).
    void uploadField(int nlon, int nlat, const unsigned char* rgba);

    struct Grid {
        float lon0, lat0, dlon, dlat;
        int nlon, nlat;
        bool globalWrap;
    };
    struct View {
        double topLeftWorldX, topLeftWorldY;
        double worldSize;  // 256 * 2^zoom
        int widthPx, heightPx;
    };

    void render(const Grid& grid, const View& view, float opacity);

private:
    std::unique_ptr<QOpenGLShaderProgram> program_;
    QOpenGLVertexArrayObject vao_;
    unsigned int fieldTex_ = 0;
    unsigned int vbo_ = 0;
    bool ready_ = false;
    bool haveField_ = false;
};

}  // namespace met::app
