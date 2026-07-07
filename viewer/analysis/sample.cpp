#include "viewer/analysis/sample.h"

#include <cmath>
#include <limits>

namespace met::analysis {
namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

float sampleBilinearIndex(const core::Field2D& field, double x, double y) {
    const int w = field.width();
    const int h = field.height();
    if (w <= 0 || h <= 0) return kNaN;
    if (x < 0.0 || y < 0.0 || x > w - 1.0 || y > h - 1.0) return kNaN;

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0;
    const double fy = y - y0;

    const float v00 = field.at(x0, y0);
    const float v10 = field.at(x1, y0);
    const float v01 = field.at(x0, y1);
    const float v11 = field.at(x1, y1);

    const double w00 = (1 - fx) * (1 - fy);
    const double w10 = fx * (1 - fy);
    const double w01 = (1 - fx) * fy;
    const double w11 = fx * fy;

    // NaN-aware: accumulate only valid corners, renormalizing by their weight.
    double acc = 0.0;
    double wsum = 0.0;
    double bestW = -1.0;
    float bestV = kNaN;
    auto add = [&](float v, double weight) {
        if (!std::isnan(v)) {
            acc += weight * v;
            wsum += weight;
            if (weight > bestW) {
                bestW = weight;
                bestV = v;
            }
        }
    };
    add(v00, w00);
    add(v10, w10);
    add(v01, w01);
    add(v11, w11);

    if (wsum <= 0.0) return kNaN;
    // If all four corners valid, this is exact bilinear. If some are missing,
    // renormalization biases toward present corners; when only one is close,
    // nearest-valid is the sensible answer.
    if (wsum > 0.999) return static_cast<float>(acc / wsum);
    return bestV;
}

float sampleBilinear(const core::Field2D& field, core::LatLon at) {
    const core::GridIndex gi = core::latlonToIndex(field.grid, at);
    if (!gi.inDomain) return kNaN;
    return sampleBilinearIndex(field, gi.x, gi.y);
}

}  // namespace met::analysis
