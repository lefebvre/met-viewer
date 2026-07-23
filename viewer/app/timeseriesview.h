#pragma once

#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QWidget>

#include "viewer/analysis/timeseries.h"

namespace met::app {

// A simple line chart of a variable's value versus time at a point.
class TimeSeriesView : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesView(QWidget* parent = nullptr);
    void setSeries(const analysis::TimeSeries& ts, const QString& varName);
    void setCurrentIndex(int index);  // highlight the current time step (marker)

    // The cursor readout currently on screen, one string per badge line; empty when
    // no readout is showing. Lets callers (and tests) read what the user is seeing.
    [[nodiscard]] QStringList hoverText() const {
        return hoverActive_ ? hoverLines_ : QStringList();
    }

    [[nodiscard]] QSize sizeHint() const override { return {640, 320}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

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

    analysis::TimeSeries ts_;
    QString varName_;
    int currentIdx_ = -1;  // marker position; -1 = none

    // Cursor readout state; hoverIdx_ is the series point the badge snapped to.
    bool hoverActive_ = false;
    int hoverIdx_ = -1;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
