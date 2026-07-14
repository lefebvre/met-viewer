#include "viewer/app/colorbarwidget.h"

#include <algorithm>

#include <QFontMetrics>
#include <QPainter>

namespace met::app {

namespace {
constexpr int kMargin = 8;   // outer padding
constexpr int kBarW = 20;    // gradient bar width
constexpr int kGap = 6;      // bar-to-label gap
}  // namespace

ColorbarWidget::ColorbarWidget(QWidget* parent) : QWidget(parent) {}

QSize ColorbarWidget::sizeHint() const {
    const QFontMetrics fm(font());
    // Width the widest tick label needs; fall back to a representative number
    // before a colormap (hence a range) has been set.
    int labelW = fm.horizontalAdvance(QStringLiteral("-8888"));
    if (cmap_) {
        for (double v : {cmap_->max(), 0.5 * (cmap_->min() + cmap_->max()), cmap_->min()})
            labelW = std::max(labelW, fm.horizontalAdvance(QString::number(v, 'g', 4)));
    }
    int w = kMargin + kBarW + kGap + labelW + kMargin;
    if (!units_.isEmpty())
        w = std::max(w, kMargin + fm.horizontalAdvance(units_) + kMargin);
    const int unitsH = units_.isEmpty() ? 0 : fm.height() + 4;
    // Keep the gradient bar comfortably tall regardless of font size.
    const int h = 2 * kMargin + unitsH + 14 * fm.height();
    return {w, h};
}

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

    const QFontMetrics fm(p.font());
    const int lineH = fm.height();
    const int barX = kMargin;
    const int barTop = kMargin;
    // Reserve a font-scaled row at the bottom for the units label so it isn't
    // clipped by the widget edge (the gradient bar stops short of the units line).
    const int unitsH = units_.isEmpty() ? 0 : lineH + 4;
    const int barH = height() - 2 * kMargin - unitsH;
    if (barH <= 0) return;

    // Gradient (top = max, bottom = min).
    for (int y = 0; y < barH; ++y) {
        const double t = 1.0 - static_cast<double>(y) / (barH - 1);
        const render::Rgba px = cmap_->mapNormalized(t);
        p.setPen(QColor(px.r, px.g, px.b));
        p.drawLine(barX, barTop + y, barX + kBarW, barTop + y);
    }
    p.setPen(palette().color(QPalette::WindowText));
    p.drawRect(barX, barTop, kBarW, barH - 1);

    // Tick labels at max / mid / min, using all width to the right of the bar so
    // they are never truncated (sizeHint reserves enough width for the panel).
    const double lo = cmap_->min();
    const double hi = cmap_->max();
    const int textX = barX + kBarW + kGap;
    const int labelW = std::max(0, width() - textX - kMargin);
    auto label = [&](double frac, double value) {
        const int y = barTop + static_cast<int>((1.0 - frac) * (barH - 1));
        p.drawLine(barX + kBarW, y, barX + kBarW + 3, y);
        QString s = QString::number(value, 'g', 4);
        p.drawText(QRect(textX, y - lineH / 2, labelW, lineH), Qt::AlignLeft | Qt::AlignVCenter, s);
    };
    label(1.0, hi);
    label(0.5, 0.5 * (lo + hi));
    label(0.0, lo);

    if (!units_.isEmpty()) {
        p.drawText(QRect(barX, height() - kMargin - unitsH, width() - 2 * kMargin, unitsH),
                   Qt::AlignLeft | Qt::AlignVCenter, units_);
    }
}

}  // namespace met::app
