#include "viewer/render/colormap.h"

#include <algorithm>
#include <cmath>

#include "viewer/render/colormaps_data.h"

namespace met::render {

Colormap::Colormap(std::string name, const std::uint8_t (*lut)[3], int size)
    : name_(std::move(name)), lut_(lut), size_(size) {}

Colormap Colormap::builtin(const std::string& name) {
    if (name == "turbo") return Colormap("turbo", detail::k_turbo, detail::kColormapSize);
    if (name == "magma") return Colormap("magma", detail::k_magma, detail::kColormapSize);
    if (name == "cividis") return Colormap("cividis", detail::k_cividis, detail::kColormapSize);
    if (name == "RdBu (diverging)")
        return Colormap("RdBu (diverging)", detail::k_RdBu_r, detail::kColormapSize);
    if (name == "coolwarm")
        return Colormap("coolwarm", detail::k_coolwarm, detail::kColormapSize);
    return Colormap("viridis", detail::k_viridis, detail::kColormapSize);
}

std::vector<std::string> Colormap::builtinNames() {
    return {"viridis", "turbo", "magma", "cividis", "RdBu (diverging)", "coolwarm"};
}

bool Colormap::isDiverging(const std::string& name) {
    return name == "RdBu (diverging)" || name == "coolwarm";
}

Rgba Colormap::mapNormalized(double t) const {
    if (!lut_ || size_ <= 0) return nan_;
    const double clamped = std::clamp(t, 0.0, 1.0);
    int idx = static_cast<int>(std::lround(clamped * (size_ - 1)));
    idx = std::clamp(idx, 0, size_ - 1);
    return Rgba{lut_[idx][0], lut_[idx][1], lut_[idx][2], 255};
}

void Colormap::fillLutRgba(std::vector<std::uint8_t>& out) const {
    out.resize(static_cast<std::size_t>(size_) * 4);
    for (int i = 0; i < size_; ++i) {
        const std::size_t o = static_cast<std::size_t>(i) * 4;
        out[o + 0] = lut_[i][0];
        out[o + 1] = lut_[i][1];
        out[o + 2] = lut_[i][2];
        out[o + 3] = 255;
    }
}

Rgba Colormap::map(double value) const {
    if (std::isnan(value)) return nan_;

    double t = 0.0;
    if (norm_ == Norm::Log) {
        const double lo = std::max(min_, 1e-30);
        const double hi = std::max(max_, lo * (1.0 + 1e-9));
        const double v = std::max(value, lo);
        t = (std::log(v) - std::log(lo)) / (std::log(hi) - std::log(lo));
    } else {
        const double span = max_ - min_;
        t = span != 0.0 ? (value - min_) / span : 0.0;
    }
    return mapNormalized(t);
}

}  // namespace met::render
