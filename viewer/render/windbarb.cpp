#include "viewer/render/windbarb.h"

#include <cmath>

namespace met::render {

BarbCount quantizeBarb(double speedKnots) {
    int n = static_cast<int>(std::lround(speedKnots / 5.0)) * 5;  // nearest 5 kt
    if (n < 0) n = 0;
    BarbCount c;
    c.pennants = n / 50;
    n %= 50;
    c.full = n / 10;
    n %= 10;
    c.half = n / 5;
    return c;
}

WindBarb makeWindBarb(QPointF origin, QPointF fromDir, double speedKnots, double length) {
    WindBarb barb;
    const BarbCount c = quantizeBarb(speedKnots);
    if (c.pennants == 0 && c.full == 0 && c.half == 0) {
        barb.calm = true;
        return barb;
    }

    // Unit staff direction (toward the source) and the perpendicular for barbs.
    const double dl = std::hypot(fromDir.x(), fromDir.y());
    if (dl < 1e-9) return barb;
    const QPointF dir(fromDir.x() / dl, fromDir.y() / dl);
    const QPointF perp(-dir.y(), dir.x());  // 90 deg CCW in screen space

    const QPointF tip = origin + dir * length;
    barb.lines.emplace_back(origin, tip);  // staff

    const double barbLen = length * 0.45;
    const double step = length * 0.16;
    // Place elements from the tip inward toward the origin.
    double pos = length;

    for (int i = 0; i < c.pennants; ++i) {
        const QPointF a = origin + dir * pos;
        const QPointF b = origin + dir * (pos - step);
        const QPointF apex = a + perp * barbLen;
        QPolygonF tri;
        tri << a << b << apex;
        barb.pennants.push_back(tri);
        pos -= step * 1.1;
    }
    for (int i = 0; i < c.full; ++i) {
        const QPointF a = origin + dir * pos;
        barb.lines.emplace_back(a, a + perp * barbLen);
        pos -= step;
    }
    for (int i = 0; i < c.half; ++i) {
        // Half barbs sit slightly in from the tip if they are the only element.
        if (pos >= length - 1e-6) pos -= step;
        const QPointF a = origin + dir * pos;
        barb.lines.emplace_back(a, a + perp * (barbLen * 0.5));
        pos -= step;
    }
    return barb;
}

}  // namespace met::render
