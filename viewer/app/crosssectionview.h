#pragma once

#include <QWidget>

#include "viewer/analysis/crosssection.h"
#include "viewer/render/colormap.h"

namespace met::app {

// Renders a vertical cross-section: distance along the path (x) vs pressure
// (y, log scale, inverted so high pressure is at the bottom), colormapped with
// isotherm-style contours.
class CrossSectionView : public QWidget {
    Q_OBJECT
public:
    explicit CrossSectionView(QWidget* parent = nullptr);
    void setSection(const analysis::CrossSection& cs);

    [[nodiscard]] QSize sizeHint() const override { return {700, 420}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    analysis::CrossSection cs_;
    render::Colormap cmap_ = render::Colormap::builtin("turbo");
    double min_ = 0, max_ = 1;
};

}  // namespace met::app
