#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
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

    analysis::CrossSection cs_;
    render::Colormap cmap_ = render::Colormap::builtin("turbo");
    double min_ = 0, max_ = 1;
    bool autoRange_ = true;

    // Rendered field cache. Each pixel costs a log-p scan of every level, so
    // rebuilding per paint would make the cursor readout's repaints unusable.
    QImage img_;
    QSize imgSize_;
    quint64 imgSection_ = 0;   // cs_ generation the image was built from
    QString imgCmap_;
    double imgMin_ = 0, imgMax_ = 0;
    quint64 sectionSeq_ = 0;   // bumped by setSection()

    // Cursor readout state; cleared when the cursor leaves.
    bool hoverActive_ = false;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
