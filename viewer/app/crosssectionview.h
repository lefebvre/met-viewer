#pragma once

#include <QString>
#include <QWidget>

#include "viewer/analysis/crosssection.h"
#include "viewer/render/colormap.h"

namespace met::app {

// Renders a vertical cross-section: distance along the path (x) vs pressure
// (y, log scale, inverted so high pressure is at the bottom), colormapped with
// isotherm-style contours. Owns its own colormap/range (independent of the field
// views) so its control panel can drive it and show a matching legend.
class CrossSectionView : public QWidget {
    Q_OBJECT
public:
    explicit CrossSectionView(QWidget* parent = nullptr);
    void setSection(const analysis::CrossSection& cs);

    void setColormapByName(const QString& name);
    void setAutoRange(bool on);            // re-fit the range to the section data
    void setRange(double lo, double hi);   // manual range
    [[nodiscard]] const render::Colormap& colormap() const { return cmap_; }
    [[nodiscard]] QString units() const { return QString::fromStdString(cs_.units); }

    [[nodiscard]] QSize sizeHint() const override { return {700, 420}; }

signals:
    // Emitted whenever the value range changes (auto-fit or manual) so a legend /
    // range spinners can follow.
    void rangeChanged(double lo, double hi);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void applyAutoRange();  // scan cs_ values -> min_/max_ + cmap_ range

    analysis::CrossSection cs_;
    render::Colormap cmap_ = render::Colormap::builtin("turbo");
    double min_ = 0, max_ = 1;
    bool autoRange_ = true;
};

}  // namespace met::app
