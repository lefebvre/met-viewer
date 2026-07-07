#pragma once

#include <QWidget>

#include "viewer/analysis/timeseries.h"

namespace met::app {

// A simple line chart of a variable's value versus time at a point.
class TimeSeriesView : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesView(QWidget* parent = nullptr);
    void setSeries(const analysis::TimeSeries& ts, const QString& varName);

    [[nodiscard]] QSize sizeHint() const override { return {640, 320}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    analysis::TimeSeries ts_;
    QString varName_;
};

}  // namespace met::app
