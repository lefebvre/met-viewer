#include "viewer/analysis/sample.h"

#include <cmath>
#include <limits>
#include <variant>

namespace met::analysis {
namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

namespace {
// True when the field's columns span the globe, so column nlon-1 is adjacent to
// column 0. latlonToIndex() returns x in [0, nlon) for such a grid, which means
// the last cell (x between nlon-1 and nlon) has its east neighbour at column 0.
bool wrapsLongitude(const core::GridDef& grid) {
    const auto* g = std::get_if<core::RegularLatLonGrid>(&grid);
    return g != nullptr && g->globalWrapLon;
}
}  // namespace

float sampleBilinearIndex(const core::Field2D& field, double x, double y) {
    const int w = field.width();
    const int h = field.height();
    if (w <= 0 || h <= 0) return kNaN;
    const bool wrapX = wrapsLongitude(field.grid);
    // Without wrapping, x past the last column is off-grid. With it, x runs up to
    // nlon and the final cell interpolates back onto column 0 — rejecting it here
    // would punch a dlon-wide NaN stripe through an otherwise global field (the
    // map warp already wraps, so the probe would disagree with what is drawn).
    const double maxX = wrapX ? static_cast<double>(w) : w - 1.0;
    if (x < 0.0 || y < 0.0 || x > maxX || y > h - 1.0) return kNaN;

    // Take the fractional parts before wrapping the cell index — wrapping first
    // and then subtracting would leave fx measured from the wrong column.
    const double xFloor = std::floor(x);
    const double yFloor = std::floor(y);
    const double fx = x - xFloor;
    const double fy = y - yFloor;

    int x0 = static_cast<int>(xFloor);
    const int y0 = static_cast<int>(yFloor);
    if (x0 >= w) x0 = wrapX ? x0 - w : w - 1;  // x exactly == nlon is column 0 again
    int x1 = x0 + 1;
    if (x1 >= w) x1 = wrapX ? 0 : w - 1;
    const int y1 = std::min(y0 + 1, h - 1);

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
