#include "viewer/app/colorbarwidget.h"

#include <algorithm>

#include <QFontMetrics>
#include <QPainter>

namespace met::app {

ColorbarWidget::ColorbarWidget(QWidget* parent) : QWidget(parent) {}

void ColorbarWidget::setColormap(const render::Colormap& cmap) {
    cmap_ = std::make_unique<render::Colormap>(cmap);
    update();
}

void ColorbarWidget::setUnits(const QString& units) {
    units_ = units;
    update();
}

void ColorbarWidget::clear() {
    cmap_.reset();
    update();
}

void ColorbarWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    if (!cmap_) return;

    const int margin = 8;
    const int labelW = 64;
    const int barW = 20;
    const int barX = margin;
    const int barTop = margin;
    // Reserve a row at the bottom for the units label so it isn't clipped by the
    // widget edge (the gradient bar stops short of the units line).
    const int unitsH = units_.isEmpty() ? 0 : 18;
    const int barH = height() - 2 * margin - unitsH;
    if (barH <= 0) return;

    // Gradient (top = max, bottom = min).
    for (int y = 0; y < barH; ++y) {
        const double t = 1.0 - static_cast<double>(y) / (barH - 1);
        const render::Rgba px = cmap_->mapNormalized(t);
        p.setPen(QColor(px.r, px.g, px.b));
        p.drawLine(barX, barTop + y, barX + barW, barTop + y);
    }
    p.setPen(palette().color(QPalette::WindowText));
    p.drawRect(barX, barTop, barW, barH - 1);

    // Tick labels at max / mid / min.
    const double lo = cmap_->min();
    const double hi = cmap_->max();
    const int textX = barX + barW + 6;
    auto label = [&](double frac, double value) {
        const int y = barTop + static_cast<int>((1.0 - frac) * (barH - 1));
        p.drawLine(barX + barW, y, barX + barW + 3, y);
        QString s = QString::number(value, 'g', 4);
        p.drawText(QRect(textX, y - 8, labelW, 16), Qt::AlignLeft | Qt::AlignVCenter, s);
    };
    label(1.0, hi);
    label(0.5, 0.5 * (lo + hi));
    label(0.0, lo);

    if (!units_.isEmpty()) {
        const int unitsW = std::max(labelW + barW, width() - 2 * margin);
        p.drawText(QRect(barX, height() - margin - unitsH, unitsW, unitsH),
                   Qt::AlignLeft | Qt::AlignVCenter, units_);
    }
}

}  // namespace met::app
