#include "viewer/analysis/derived.h"

#include <cmath>
#include <limits>
#include <variant>

namespace met::analysis {
namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr double kR = 6371229.0;  // earth radius (m), matching the readers' sphere

core::Field2D likeField(const core::Field2D& src, const std::string& var, const std::string& units,
                        const std::string& longName) {
    core::Field2D f;
    f.grid = src.grid;
    f.values.assign(src.values.size(), kNaN);
    f.meta = src.meta;
    f.meta.varName = var;
    f.meta.units = units;
    f.meta.longName = longName;
    f.meta.standardName.clear();
    return f;
}

// Signed grid spacing (metres) per column step (east-ish) and row step
// (north-ish) at cell (i, j). Handles regular lat/lon (with the cos-lat metric)
// and projected grids (spacing already in metres).
void gridSpacing(const core::GridDef& grid, int i, int j, double& dxStep, double& dyStep) {
    if (const auto* g = std::get_if<core::RegularLatLonGrid>(&grid)) {
        const double lat = g->lat0 + g->dlat * j;
        const double d2r = M_PI / 180.0;
        dxStep = g->dlon * d2r * kR * std::cos(lat * d2r);
        dyStep = g->dlat * d2r * kR;
    } else {
        const auto& p = std::get<core::ProjectedGrid>(grid);
        (void)i;
        (void)j;
        dxStep = p.dx;
        dyStep = p.dy;
    }
}

// Centered horizontal derivatives of a component at (i, j). Returns false at the
// domain boundary or when a neighbour is missing.
bool centralDerivatives(const core::Field2D& comp, int i, int j, double dxStep, double dyStep,
                        double& ddx, double& ddy) {
    const int w = comp.width(), h = comp.height();
    if (i <= 0 || j <= 0 || i >= w - 1 || j >= h - 1) return false;
    const float xp = comp.at(i + 1, j), xm = comp.at(i - 1, j);
    const float yp = comp.at(i, j + 1), ym = comp.at(i, j - 1);
    if (std::isnan(xp) || std::isnan(xm) || std::isnan(yp) || std::isnan(ym)) return false;
    ddx = (xp - xm) / (2.0 * dxStep);
    ddy = (yp - ym) / (2.0 * dyStep);
    return true;
}

}  // namespace

core::Field2D windSpeedField(const WindField& w) {
    core::Field2D f = likeField(w.u, "wspd", "m/s", "Wind speed");
    for (std::size_t k = 0; k < f.values.size(); ++k) {
        const float u = w.u.values[k], v = w.v.values[k];
        if (!std::isnan(u) && !std::isnan(v)) f.values[k] = std::sqrt(u * u + v * v);
    }
    return f;
}

core::Field2D windDirectionField(const WindField& w) {
    core::Field2D f = likeField(w.u, "wdir", "deg", "Wind direction (from)");
    for (std::size_t k = 0; k < f.values.size(); ++k) {
        const float u = w.u.values[k], v = w.v.values[k];
        if (std::isnan(u) || std::isnan(v)) continue;
        double dir = std::atan2(-u, -v) * 180.0 / M_PI;  // direction wind comes FROM
        if (dir < 0) dir += 360.0;
        f.values[k] = static_cast<float>(dir);
    }
    return f;
}

core::Field2D potentialTemperatureField(const core::Field2D& tempK, double pressureHPa) {
    core::Field2D f = likeField(tempK, "theta", "K", "Potential temperature");
    if (pressureHPa <= 0) return f;
    const double factor = std::pow(1000.0 / pressureHPa, 0.2854);
    for (std::size_t k = 0; k < f.values.size(); ++k) {
        const float t = tempK.values[k];
        if (!std::isnan(t)) f.values[k] = static_cast<float>(t * factor);
    }
    return f;
}

core::Field2D relativeVorticityField(const WindField& w) {
    core::Field2D f = likeField(w.u, "vo", "s**-1", "Relative vorticity");
    const int width = w.width(), height = w.height();
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            double dxStep, dyStep;
            gridSpacing(w.u.grid, i, j, dxStep, dyStep);
            double dudx, dudy, dvdx, dvdy;
            if (!centralDerivatives(w.u, i, j, dxStep, dyStep, dudx, dudy)) continue;
            if (!centralDerivatives(w.v, i, j, dxStep, dyStep, dvdx, dvdy)) continue;
            f.values[static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(i)] = static_cast<float>(dvdx - dudy);
        }
    }
    return f;
}

core::Field2D divergenceField(const WindField& w) {
    core::Field2D f = likeField(w.u, "div", "s**-1", "Divergence");
    const int width = w.width(), height = w.height();
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            double dxStep, dyStep;
            gridSpacing(w.u.grid, i, j, dxStep, dyStep);
            double dudx, dudy, dvdx, dvdy;
            if (!centralDerivatives(w.u, i, j, dxStep, dyStep, dudx, dudy)) continue;
            if (!centralDerivatives(w.v, i, j, dxStep, dyStep, dvdx, dvdy)) continue;
            f.values[static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(i)] = static_cast<float>(dudx + dvdy);
        }
    }
    return f;
}

}  // namespace met::analysis
