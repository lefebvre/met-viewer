#pragma once

#include <vector>

#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "viewer/analysis/crosssection.h"
#include "viewer/render/colormap.h"

class QPainter;

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
    void setAutoRange(bool on);           // re-fit the range to the section data
    void setRange(double lo, double hi);  // manual range
    // Draw isopleths of geopotential height over the section. A pressure surface
    // tilts along the path, so height cannot be a second axis here — it is its own
    // field, contoured like one. No-op for a section with no height data.
    void setHeightContoursEnabled(bool on);
    [[nodiscard]] bool heightContoursEnabled() const { return showHeights_; }
    // Whether the section carries geopotential height at all, i.e. whether the
    // contour toggle has anything to show.
    [[nodiscard]] bool hasHeights() const { return !cs_.heights.empty(); }
    // Decimals for the cursor readout's lat/lon, from the source grid spacing
    // (see app::coordPrecision). The section carries no grid, so MainWindow sets it.
    void setCoordPrecision(int digits) { coordPrec_ = digits; }
    [[nodiscard]] const render::Colormap& colormap() const { return cmap_; }
    [[nodiscard]] QString units() const { return QString::fromStdString(cs_.units); }

    // The cursor readout currently on screen, one string per badge line; empty when
    // no readout is showing. Lets callers (and tests) read what the user is seeing.
    [[nodiscard]] QStringList hoverText() const {
        return hoverActive_ ? hoverLines_ : QStringList();
    }

    [[nodiscard]] QSize sizeHint() const override { return {700, 420}; }

signals:
    // Emitted whenever the value range changes (auto-fit or manual) so a legend /
    // range spinners can follow.
    void rangeChanged(double lo, double hi);
    // Emitted on every setSection with whether that section carries geopotential
    // height. The control panel is built before the first section arrives, so the
    // height-contour toggle learns whether it has anything to show from here.
    void heightsAvailableChanged(bool available);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void applyAutoRange();  // scan cs_ values -> min_/max_ + cmap_ range

    // The plot area and pressure axis, derived from the widget size and the
    // section's data. Shared by paintEvent and the cursor readout so the drawn
    // image and the hovered value can never disagree about the mapping.
    struct Layout {
        QRectF rect;
        double pTop = 0, pBot = 0;  // hPa
        bool valid = false;
    };
    [[nodiscard]] Layout layout() const;
    void rebuildImage(const Layout& lay);  // regenerate img_ if its key changed

    // One height isopleth: its value (gpm) and the line segments that draw it, in
    // widget coordinates.
    struct HeightContour {
        double gpm = 0;
        std::vector<QLineF> lines;
    };
    void rebuildHeightContours(const Layout& lay);
    void paintHeightContours(QPainter& p, const QRectF& r) const;

    analysis::CrossSection cs_;
    render::Colormap cmap_ = render::Colormap::builtin("turbo");
    double min_ = 0, max_ = 1;
    bool autoRange_ = true;
    int coordPrec_ = 2;  // lat/lon decimals in the cursor readout

    // Rendered field cache. Each pixel costs a log-p scan of every level, so
    // rebuilding per paint would make the cursor readout's repaints unusable.
    QImage img_;
    QSize imgSize_;
    quint64 imgSection_ = 0;  // cs_ generation the image was built from
    QString imgCmap_;
    double imgMin_ = 0, imgMax_ = 0;
    quint64 sectionSeq_ = 0;  // bumped by setSection()

    // Height isopleths, cached on the same terms as img_ and for the same reason:
    // they come from marching squares over a resampled lattice, which is far too
    // much work to repeat on the repaint that follows every mouse-move.
    bool showHeights_ = true;
    std::vector<HeightContour> heightContours_;
    QSize contourSize_;
    quint64 contourSection_ = 0;  // 0 = nothing built (sectionSeq_ starts at 1)

    // Cursor readout state; cleared when the cursor leaves.
    bool hoverActive_ = false;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
