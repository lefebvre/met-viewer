#include "viewer/app/hoverreadout.h"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QSettings>

#include "viewer/core/units.h"

namespace met::app {
namespace {

// QSettings key for a view's flag. Stable strings (not the enum's numeric value)
// so reordering HoverView never silently re-reads someone else's setting.
const char* settingsKey(HoverView v) {
    switch (v) {
        case HoverView::Plot: return "hover/plot";
        case HoverView::Map: return "hover/map";
        case HoverView::CrossSection: return "hover/crossSection";
        case HoverView::TimeSeries: return "hover/timeSeries";
        case HoverView::SkewT: return "hover/skewT";
    }
    return "hover/unknown";
}

constexpr int kPad = 5;       // text box inner padding
constexpr int kCursorGap = 12;  // offset from the cursor to the box corner

}  // namespace

HoverOptions::HoverOptions() {
    QSettings s;
    for (int i = 0; i < 5; ++i)
        enabled_[i] = s.value(settingsKey(static_cast<HoverView>(i)), true).toBool();
}

HoverOptions& HoverOptions::instance() {
    static HoverOptions options;
    return options;
}

bool HoverOptions::enabled(HoverView v) const { return enabled_[static_cast<int>(v)]; }

void HoverOptions::setEnabled(HoverView v, bool on) {
    if (enabled_[static_cast<int>(v)] == on) return;
    enabled_[static_cast<int>(v)] = on;
    QSettings().setValue(settingsKey(v), on);
    emit changed(v);
}

void paintHoverReadout(QPainter& p, const QRectF& plotRect, QPointF at,
                       const QStringList& lines, const QPalette& pal) {
    if (lines.isEmpty() || !plotRect.contains(at)) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    // The placement below keeps the box inside `plotRect` whenever it fits; clipping
    // makes that unconditional, so a long readout in a narrow view is truncated
    // rather than scribbled over the axis labels.
    p.setClipRect(plotRect, Qt::IntersectClip);

    // Crosshair, clipped to the plot area.
    QPen cross(pal.color(QPalette::Text), 0.8);
    cross.setStyle(Qt::DashLine);
    p.setPen(cross);
    p.drawLine(QPointF(plotRect.left(), at.y()), QPointF(plotRect.right(), at.y()));
    p.drawLine(QPointF(at.x(), plotRect.top()), QPointF(at.x(), plotRect.bottom()));

    // Text box, sized to the widest line.
    const QFontMetrics fm(p.font());
    int textW = 0;
    for (const QString& line : lines) textW = std::max(textW, fm.horizontalAdvance(line));
    const int rowH = fm.height();
    const double boxW = textW + 2.0 * kPad;
    const double boxH = rowH * static_cast<double>(lines.size()) + 2.0 * kPad;

    // Prefer down-right of the cursor; flip to the other side when that would spill
    // out of the plot area, then clamp so the box is never clipped in a corner.
    double bx = at.x() + kCursorGap;
    double by = at.y() + kCursorGap;
    if (bx + boxW > plotRect.right()) bx = at.x() - kCursorGap - boxW;
    if (by + boxH > plotRect.bottom()) by = at.y() - kCursorGap - boxH;
    bx = std::clamp(bx, plotRect.left(), std::max(plotRect.left(), plotRect.right() - boxW));
    by = std::clamp(by, plotRect.top(), std::max(plotRect.top(), plotRect.bottom() - boxH));
    const QRectF box(bx, by, boxW, boxH);

    QColor bg = pal.color(QPalette::Base);
    bg.setAlpha(215);
    p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    p.setBrush(bg);
    p.drawRect(box);
    p.setBrush(Qt::NoBrush);

    p.setPen(pal.color(QPalette::Text));
    double y = box.top() + kPad;
    for (const QString& line : lines) {
        p.drawText(QRectF(box.left() + kPad, y, textW, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                   line);
        y += rowH;
    }

    p.restore();
}

QString formatValueWithUnits(double value, const QString& units) {
    if (std::isnan(value)) return QStringLiteral("—");
    QString s = QString::number(value, 'f', 2);
    if (units.isEmpty()) return s;
    s += QLatin1Char(' ') + units;
    if (const auto alt = core::preferredDisplayUnit(units.toStdString())) {
        if (const auto converted = core::convert(value, units.toStdString(), *alt)) {
            s += QStringLiteral(" (%1 %2)")
                     .arg(*converted, 0, 'f', 2)
                     .arg(QString::fromStdString(core::unitLabel(*alt)));
        }
    }
    return s;
}

}  // namespace met::app
