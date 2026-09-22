#pragma once

#include <utility>

namespace met::app {

// One plot axis's limits: fitted to the data, unless the user has pinned them.
//
// Deliberately the same shape as the colormap's auto/manual range that every
// colourmapped view already carries (setAutoRange / setRange / rangeChanged), so an
// axis behaves the way the colour scale beside it does: a checkbox that hands the
// axis back to the data, and a pair of numbers that take it away again.
//
// Holding the pinned pair separately from the fit is what lets a view answer
// `resolve()` without the data having to be re-extracted when the checkbox moves,
// and what lets the control panel seed its spin boxes from the fit before the user
// ever edits them — so unchecking "Auto" does not make the plot jump.
class AxisRange {
public:
    [[nodiscard]] bool automatic() const { return auto_; }
    void setAutomatic(bool on) { auto_ = on; }

    // Pin the axis to `lo`..`hi`. Whether that pin is in force is `automatic()`'s
    // business, not this one's: the control panel drives the two independently, the
    // same way the colormap controls drive theirs.
    //
    // A pair that is not a range is dropped rather than stored inverted or
    // swapped. A spin box passes through half-typed states on the way to a value,
    // and an axis that briefly drew itself upside down on the way would be worse
    // than one that waits for the second number.
    void set(double lo, double hi) {
        if (!(lo < hi)) return;
        lo_ = lo;
        hi_ = hi;
        pinned_ = true;
    }

    // The limits to draw with, given what the data alone asks for.
    //
    // An axis switched to manual before it was ever given a pair still draws the
    // fit: the alternative is a plot that empties itself the moment a checkbox is
    // unticked, which reads as a bug rather than as a setting.
    [[nodiscard]] std::pair<double, double> resolve(double fitLo, double fitHi) const {
        return (auto_ || !pinned_) ? std::pair{fitLo, fitHi} : std::pair{lo_, hi_};
    }

private:
    bool auto_ = true;
    bool pinned_ = false;
    double lo_ = 0.0, hi_ = 1.0;
};

}  // namespace met::app
