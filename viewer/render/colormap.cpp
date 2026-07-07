#include "viewer/render/colormap.h"

#include <algorithm>
#include <cmath>

#include "viewer/render/colormaps_data.h"

namespace met::render {

Colormap::Colormap(std::string name, const std::uint8_t (*lut)[3], int size)
    : name_(std::move(name)), lut_(lut), size_(size) {}

Colormap Colormap::builtin(const std::string& name) {
    if (name == "turbo") return Colormap("turbo", detail::k_turbo, detail::kColormapSize);
    return Colormap("viridis", detail::k_viridis, detail::kColormapSize);
}

std::vector<std::string> Colormap::builtinNames() { return {"viridis", "turbo"}; }

Rgba Colormap::mapNormalized(double t) const {
    if (!lut_ || size_ <= 0) return nan_;
    const double clamped = std::clamp(t, 0.0, 1.0);
    int idx = static_cast<int>(std::lround(clamped * (size_ - 1)));
    idx = std::clamp(idx, 0, size_ - 1);
    return Rgba{lut_[idx][0], lut_[idx][1], lut_[idx][2], 255};
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
