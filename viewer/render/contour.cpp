#include "viewer/render/contour.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace met::render {
namespace {

// Linear interpolation factor where `level` crosses between values a and b.
double crossFrac(double a, double b, double level) {
    const double d = b - a;
    if (d == 0.0) return 0.5;
    return (level - a) / d;
}

}  // namespace

std::vector<ContourSegment> contourAt(const core::Field2D& field, double level) {
    std::vector<ContourSegment> out;
    const int w = field.width();
    const int h = field.height();
    if (w < 2 || h < 2) return out;

    for (int j = 0; j < h - 1; ++j) {
        for (int i = 0; i < w - 1; ++i) {
            // Corner values, clockwise from bottom-left in index space:
            //   (i, j+1) TL ---- (i+1, j+1) TR
            //      |                  |
            //   (i, j)   BL ---- (i+1, j)   BR
            const double bl = field.at(i, j);
            const double br = field.at(i + 1, j);
            const double tr = field.at(i + 1, j + 1);
            const double tl = field.at(i, j + 1);
            if (std::isnan(bl) || std::isnan(br) || std::isnan(tr) || std::isnan(tl)) continue;

            // Marching-squares case: bit per corner above the level.
            int c = 0;
            if (bl >= level) c |= 1;
            if (br >= level) c |= 2;
            if (tr >= level) c |= 4;
            if (tl >= level) c |= 8;
            if (c == 0 || c == 15) continue;

            // Edge crossing points (in index space).
            const double xi = static_cast<double>(i);
            const double yj = static_cast<double>(j);
            // Bottom edge BL-BR (varies in x at y=j)
            auto eB = [&] { return std::pair{xi + crossFrac(bl, br, level), yj}; };
            // Right edge BR-TR (varies in y at x=i+1)
            auto eR = [&] { return std::pair{xi + 1.0, yj + crossFrac(br, tr, level)}; };
            // Top edge TL-TR (varies in x at y=j+1)
            auto eT = [&] { return std::pair{xi + crossFrac(tl, tr, level), yj + 1.0}; };
            // Left edge BL-TL (varies in y at x=i)
            auto eL = [&] { return std::pair{xi, yj + crossFrac(bl, tl, level)}; };

            auto emit = [&](std::pair<double, double> p, std::pair<double, double> q) {
                out.push_back({p.first, p.second, q.first, q.second});
            };

            switch (c) {
                case 1: case 14: emit(eL(), eB()); break;
                case 2: case 13: emit(eB(), eR()); break;
                case 3: case 12: emit(eL(), eR()); break;
                case 4: case 11: emit(eR(), eT()); break;
                case 6: case 9:  emit(eB(), eT()); break;
                case 7: case 8:  emit(eL(), eT()); break;
                // Saddle cases: connect both pairs (ambiguity resolved arbitrarily).
                case 5:  emit(eL(), eB()); emit(eR(), eT()); break;
                case 10: emit(eB(), eR()); emit(eL(), eT()); break;
                default: break;
            }
        }
    }
    return out;
}

double niceContourInterval(double lo, double hi, int target) {
    const double range = hi - lo;
    if (!(range > 0.0) || target <= 0) return 0.0;
    const double raw = range / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double step = 10.0;
    if (norm < 1.5) step = 1.0;
    else if (norm < 3.0) step = 2.0;
    else if (norm < 7.0) step = 5.0;
    return step * mag;
}

std::vector<ContourLevel> contourLevels(const core::Field2D& field, double interval) {
    std::vector<ContourLevel> out;
    if (!(interval > 0.0)) return out;

    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (float v : field.values) {
        if (std::isnan(v)) continue;
        lo = std::min(lo, static_cast<double>(v));
        hi = std::max(hi, static_cast<double>(v));
    }
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo == hi) return out;

    // First contour strictly above the minimum (a level exactly at lo is
    // degenerate — every corner is >= it, so it yields no segments).
    double first = std::floor(lo / interval) * interval;
    if (first <= lo) first += interval;
    // Guard against pathological interval/range combos producing huge counts.
    const int maxLines = 200;
    int count = 0;
    for (double level = first; level <= hi && count < maxLines; level += interval, ++count) {
        out.push_back({level, contourAt(field, level)});
    }
    return out;
}

}  // namespace met::render
