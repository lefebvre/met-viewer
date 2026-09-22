#include "viewer/app/exportname.h"

#include <cmath>
#include <string>

#include <QStringList>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/sounding.h"
#include "viewer/analysis/timeseries.h"
#include "viewer/core/timeaxis.h"

namespace met::app {
namespace {
QString number(double v) { return QString::number(v, 'g', 6); }

// "m3" for an ensemble member, nothing for a deterministic field, so the common
// case keeps the shorter name.
void appendMember(QStringList& parts, int member) {
    if (member >= 0) parts << QStringLiteral("m%1").arg(member);
}

QString timeTag(core::TimePoint t) { return QString::fromStdString(core::compactTime(t)); }

// The variable, when the data says which one it is.
void appendVar(QStringList& parts, const std::string& var) {
    if (!var.empty()) parts << fileSafe(QString::fromStdString(var));
}
}  // namespace

QString latLonTag(core::LatLon p) {
    return QStringLiteral("%1%2_%3%4")
        .arg(QString::number(std::abs(p.lat), 'f', 2),
             p.lat >= 0.0 ? QStringLiteral("N") : QStringLiteral("S"),
             QString::number(std::abs(p.lon), 'f', 2),
             p.lon >= 0.0 ? QStringLiteral("E") : QStringLiteral("W"));
}

QString levelTag(const core::VerticalLevel& lvl) {
    using T = core::VerticalLevel::Type;
    switch (lvl.type) {
        case T::Surface:
            return QStringLiteral("surface");
        case T::PressureHPa:
            return number(lvl.value) + QStringLiteral("hPa");
        case T::HeightM:
            return number(lvl.value) + QStringLiteral("m");
        case T::ModelLevel:
            return QStringLiteral("ml") + number(lvl.value);
        case T::Sigma:
            return QStringLiteral("sigma") + number(lvl.value);
        case T::Hybrid:
            return QStringLiteral("hybrid") + number(lvl.value);
        case T::Isentropic:
            return number(lvl.value) + QStringLiteral("K");
        case T::Unknown:
            break;
    }
    return QStringLiteral("level") + number(lvl.value);
}

QString fileSafe(const QString& s) {
    QString out = s;
    for (QChar& c : out) {
        const bool keep = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                          (c >= u'0' && c <= u'9') || c == u'.' || c == u'_' || c == u'-';
        if (!keep) c = u'-';
    }
    return out;
}

QString plotFigureStem(const core::Field2D* field) {
    QStringList parts{QStringLiteral("plot")};
    if (field) {
        appendVar(parts, field->meta.varName);
        parts << levelTag(field->meta.level) << timeTag(field->meta.validTime);
        appendMember(parts, field->meta.member);
    }
    return parts.join('_');
}

QString soundingFigureStem(const analysis::Sounding& s) {
    QStringList parts{QStringLiteral("skewt")};
    // Gated on there being levels, never on the time's value: a sounding with levels
    // came through extractSounding, which stamps it, and any sentinel time would be
    // a real instant (see PointProfileDock::suggestedCsvName).
    if (!s.levels.empty()) {
        parts << latLonTag(s.point) << timeTag(s.validTime);
        appendMember(parts, s.member);
    }
    return parts.join('_');
}

QString sectionFigureStem(const analysis::CrossSection& cs) {
    QStringList parts{QStringLiteral("section")};
    if (!cs.points.empty()) {
        // Both ends, so two sections of the same field at the same time do not
        // default to the same name.
        appendVar(parts, cs.varName);
        parts << latLonTag(cs.points.front()) << QStringLiteral("to") << latLonTag(cs.points.back())
              << timeTag(cs.validTime);
        appendMember(parts, cs.member);
    }
    return parts.join('_');
}

QString seriesFigureStem(const analysis::TimeSeries& ts) {
    QStringList parts{QStringLiteral("series")};
    // No time: the series spans all of them.
    if (!ts.times.empty()) {
        appendVar(parts, ts.varName);
        parts << levelTag(ts.level) << latLonTag(ts.point);
        appendMember(parts, ts.member);
    }
    return parts.join('_');
}

}  // namespace met::app
