#pragma once

#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QWidget>

#include "viewer/analysis/timeseries.h"
#include "viewer/app/axisrange.h"

namespace met::app {

// A simple line chart of a variable's value versus time at a point.
class TimeSeriesView : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesView(QWidget* parent = nullptr);
    void setSeries(const analysis::TimeSeries& ts, const QString& varName);
    void setCurrentIndex(int index);  // highlight the current time step (marker)
    // Decimals for the point's lat/lon in the title, from the source grid spacing
    // (see app::coordPrecision). The series carries no grid, so MainWindow sets it.
    void setCoordPrecision(int digits) { coordPrec_ = digits; }

    // Value-axis limits. Automatic fits the series and pads it; pinned is taken
    // literally, and the trace is clipped to the box rather than drawn outside it.
    // The time axis is not settable here: it is the steps the series was built
    // from, and narrowing it is a question about the extraction, not the plot.
    void setValueAuto(bool on);
    void setValueLimits(double lo, double hi);

    // The cursor readout currently on screen, one string per badge line; empty when
    // no readout is showing. Lets callers (and tests) read what the user is seeing.
    [[nodiscard]] QStringList hoverText() const {
        return hoverActive_ ? hoverLines_ : QStringList();
    }

    [[nodiscard]] QSize sizeHint() const override { return {640, 320}; }

signals:
    // The value axis actually drawn, fitted or pinned, so the control panel's spin
    // boxes can follow the fit while they are not the ones driving it.
    void valueRangeChanged(double lo, double hi);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // The plot area and the padded value axis. Shared by paintEvent and the cursor
    // readout so the drawn points and the hovered sample agree on the mapping.
    struct Layout {
        QRectF rect;
        double lo = 0, hi = 1;  // padded value range
        bool valid = false;

        [[nodiscard]] double xOf(int i, int n) const {
            return n > 1 ? rect.left() + rect.width() * i / (n - 1) : rect.left();
        }
        [[nodiscard]] double yOf(double v) const {
            return rect.bottom() - (v - lo) / (hi - lo) * rect.height();
        }
    };
    [[nodiscard]] Layout layout() const;
    void publishRange();  // re-read the drawn axis and emit it, if it moved

    analysis::TimeSeries ts_;
    AxisRange value_;
    // The axis last emitted, so an unchanged one is not re-announced into a spin
    // box the user may be part-way through typing in.
    double sentLo_ = 0, sentHi_ = 0;
    QString varName_;
    int currentIdx_ = -1;  // marker position; -1 = none
    int coordPrec_ = 2;    // lat/lon decimals in the title

    // Cursor readout state; hoverIdx_ is the series point the badge snapped to.
    bool hoverActive_ = false;
    int hoverIdx_ = -1;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
