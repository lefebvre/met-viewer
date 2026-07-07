#include "viewer/analysis/derived.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
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

// Latitude (radians) at grid row j for a regular lat/lon grid, or NaN for a
// projected grid — whose planar (metre) derivatives already carry the metric,
// so no spherical curvature correction is applied there.
double gridLatRadians(const core::GridDef& grid, int j) {
    if (const auto* g = std::get_if<core::RegularLatLonGrid>(&grid))
        return (g->lat0 + g->dlat * j) * M_PI / 180.0;
    return std::numeric_limits<double>::quiet_NaN();
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

std::optional<core::Field2D> asTemperatureKelvin(const core::Field2D& f) {
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string sn = lower(f.meta.standardName);
    const std::string vn = lower(f.meta.varName);
    const std::string& u = f.meta.units;  // keep case: 'K' is distinct from 'k'
    const std::string ul = lower(u);

    const bool nameTemp = sn == "air_temperature" || sn == "temperature" || vn == "t" ||
                          vn == "temp" || vn == "t2m" || vn == "2t" || vn == "tmp";
    const bool kelvin = u == "K" || ul == "kelvin" || ul == "degk" || ul == "deg_k";
    const bool celsius = ul == "c" || ul == "degc" || ul == "celsius" ||
                         ul == "degrees_celsius" || ul == "deg_c" || u == "°C";

    // Reject clearly non-temperature fields (geopotential, wind, RH, pressure…).
    if (!nameTemp && !kelvin && !celsius) return std::nullopt;

    core::Field2D out = f;
    if (celsius)
        for (float& v : out.values)
            if (!std::isnan(v)) v += 273.15f;
    return out;
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
            // ζ = ∂v/∂x − ∂u/∂y + (u/R)·tanφ. The last term is the spherical
            // metric (map-factor) correction, applied only on lat/lon grids.
            double vort = dvdx - dudy;
            const double phi = gridLatRadians(w.u.grid, j);
            const float uc = w.u.at(i, j);
            if (!std::isnan(phi) && !std::isnan(uc)) vort += uc * std::tan(phi) / kR;
            f.values[static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(i)] = static_cast<float>(vort);
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
            // δ = ∂u/∂x + ∂v/∂y − (v/R)·tanφ (spherical metric term on lat/lon).
            double div = dudx + dvdy;
            const double phi = gridLatRadians(w.u.grid, j);
            const float vc = w.v.at(i, j);
            if (!std::isnan(phi) && !std::isnan(vc)) div -= vc * std::tan(phi) / kR;
            f.values[static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(i)] = static_cast<float>(div);
        }
    }
    return f;
}

}  // namespace met::analysis
