#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace met::render {

// An RGBA color, 8 bits per channel, non-premultiplied.
struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

// Normalization from data value to the [0, 1] LUT position.
enum class Norm { Linear, Log };

// A named colormap: a 256-entry RGB lookup table plus a value range and
// normalization. Mapping a value clamps to [min, max], normalizes, and looks up
// the LUT. NaN maps to a configurable transparent/gray color.
class Colormap {
public:
    // Built-in maps by name ("viridis", "turbo"). Unknown names fall back to
    // viridis.
    static Colormap builtin(const std::string& name);

    // Names of the available built-in maps.
    static std::vector<std::string> builtinNames();

    void setRange(double lo, double hi) {
        min_ = lo;
        max_ = hi;
    }
    void setNorm(Norm n) { norm_ = n; }

    [[nodiscard]] double min() const { return min_; }
    [[nodiscard]] double max() const { return max_; }
    [[nodiscard]] const std::string& name() const { return name_; }

    // Map a single value to a color. NaN -> nanColor().
    [[nodiscard]] Rgba map(double value) const;

    // Map a LUT position already in [0, 1] (used by the colorbar).
    [[nodiscard]] Rgba mapNormalized(double t) const;

    [[nodiscard]] Rgba nanColor() const { return nan_; }

private:
    Colormap(std::string name, const std::uint8_t (*lut)[3], int size);

    std::string name_;
    const std::uint8_t (*lut_)[3] = nullptr;
    int size_ = 0;
    double min_ = 0.0;
    double max_ = 1.0;
    Norm norm_ = Norm::Linear;
    Rgba nan_{0, 0, 0, 0};  // transparent
};

}  // namespace met::render
