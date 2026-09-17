#include "viewer/app/pointprofilemodel.h"

#include <algorithm>
#include <cmath>

#include <QColor>

#include "viewer/app/hoverreadout.h"
#include "viewer/core/timeaxis.h"
#include "viewer/core/units.h"

namespace met::app {
namespace {

// What a cell with no number means, in the words a reader can act on. This is
// the payoff for CellStatus: without it every one of these would be the same
// blank, and the reader could not tell "move the point" from "distrust the file".
QString statusExplanation(analysis::CellStatus st) {
    switch (st) {
        case analysis::CellStatus::Ok:
            return {};
        case analysis::CellStatus::NoRecord:
            return QObject::tr("This variable has no data at this level.");
        case analysis::CellStatus::OutsideGrid:
            return QObject::tr("This point lies outside the data's grid.");
        case analysis::CellStatus::Missing:
            return QObject::tr("The data around this point is missing.");
        case analysis::CellStatus::Unconvertible:
            return QObject::tr("The field's units could not be placed, so no altitude is shown.");
    }
    return {};
}

// The same formatter the CSV uses, so a cell and its exported field can never
// show a different number of digits for the same value.
QString cellNumber(double value, const std::string& units) {
    const std::string s = analysis::formatExportValue(value, units);
    return s.empty() ? QStringLiteral("—") : QString::fromStdString(s);
}

}  // namespace

PointProfileModel::PointProfileModel(QObject* parent) : QAbstractTableModel(parent) {}

void PointProfileModel::setProfile(const analysis::PointProfile& profile) {
    beginResetModel();
    profile_ = profile;
    endResetModel();
}

void PointProfileModel::clearProfile() {
    beginResetModel();
    profile_ = {};
    endResetModel();
}

void PointProfileModel::setUnitChoice(const std::map<std::string, std::string>& choice) {
    beginResetModel();  // every cell's text and every header changes
    unitChoice_ = choice;
    endResetModel();
}

int PointProfileModel::heightColumn() const { return profile_.heightSourceVar.empty() ? -1 : 2; }

std::string PointProfileModel::nativeUnitAt(int column) const {
    if (column == pressureColumn()) return "hPa";
    if (column == heightColumn()) return "gpm";
    const int c = column - firstValueColumn();
    if (c < 0 || static_cast<std::size_t>(c) >= profile_.columns.size()) return {};
    return profile_.columns[static_cast<std::size_t>(c)].units;
}

std::string PointProfileModel::columnKeyAt(int column) const {
    if (column == pressureColumn()) return kPressureKey;
    if (column == heightColumn()) return kHeightKey;
    const int c = column - firstValueColumn();
    if (c < 0 || static_cast<std::size_t>(c) >= profile_.columns.size()) return {};
    return profile_.columns[static_cast<std::size_t>(c)].id;
}

QString PointProfileModel::columnNameAt(int column) const {
    if (column == pressureColumn()) return tr("Pressure");
    if (column == heightColumn()) return tr("Height MSL");
    const int c = column - firstValueColumn();
    if (c < 0 || static_cast<std::size_t>(c) >= profile_.columns.size()) return {};
    const analysis::ProfileColumn& pc = profile_.columns[static_cast<std::size_t>(c)];
    return QString::fromStdString(pc.longName.empty() ? pc.id : pc.longName);
}

std::string PointProfileModel::displayUnitAt(int column) const {
    const std::string native = nativeUnitAt(column);
    if (native.empty()) return {};
    const auto it = unitChoice_.find(columnKeyAt(column));
    if (it == unitChoice_.end()) return native;
    // alternativeUnits() leads with the native unit in its canonical spelling, so
    // choosing it keeps the file's own spelling in the header rather than
    // swapping "m s**-1" for "m/s" on a column that was never converted.
    const std::vector<std::string> alts = core::alternativeUnits(native);
    if (it->second == alts.front()) return native;
    return std::find(alts.begin(), alts.end(), it->second) != alts.end() ? it->second : native;
}

std::vector<PointProfileModel::UnitColumn> PointProfileModel::unitColumns() const {
    std::vector<UnitColumn> out;
    for (int c = 1; c < columnCount(); ++c) {
        std::string native = nativeUnitAt(c);
        if (native.empty()) continue;
        out.push_back({columnKeyAt(c), columnNameAt(c), std::move(native), displayUnitAt(c)});
    }
    return out;
}

analysis::ProfileUnits PointProfileModel::exportUnits() const {
    analysis::ProfileUnits units;
    units.pressure = displayUnitAt(pressureColumn());
    if (heightColumn() >= 0) units.height = displayUnitAt(heightColumn());
    units.columns.reserve(profile_.columns.size());
    for (std::size_t c = 0; c < profile_.columns.size(); ++c)
        units.columns.push_back(displayUnitAt(firstValueColumn() + static_cast<int>(c)));
    return units;
}

int PointProfileModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(profile_.levels.size());
}

int PointProfileModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    if (profile_.levels.empty()) return 0;
    return firstValueColumn() + static_cast<int>(profile_.columns.size());
}

QVariant PointProfileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount()) return {};
    const analysis::ProfileLevel& row = profile_.levels[static_cast<std::size_t>(index.row())];
    const int col = index.column();

    if (role == kRowIndexRole) return index.row();

    // The level label is text, so it sorts by its own numeric coordinate rather
    // than alphabetically -- "1000 hPa" would otherwise sort before "850 hPa".
    if (col == 0) {
        if (role == Qt::DisplayRole || role == kExportRole)
            return QString::fromStdString(core::formatLevel(row.level));
        if (role == kSortRole) return core::levelSortKey(row.level);
        return {};
    }

    double native = std::numeric_limits<double>::quiet_NaN();
    analysis::CellStatus status = analysis::CellStatus::NoRecord;
    if (col == pressureColumn()) {
        native = row.pressureHpa;
        status = row.pressureStatus;
    } else if (col == heightColumn()) {
        native = static_cast<double>(row.heightGpm);
        status = row.heightStatus;
    } else {
        const std::size_t c = static_cast<std::size_t>(col - firstValueColumn());
        if (c < row.values.size()) {
            native = static_cast<double>(row.values[c]);
            status = row.statuses[c];
        }
    }

    const std::string nativeUnit = nativeUnitAt(col);
    const std::string showUnit = displayUnitAt(col);
    double shown = native;
    if (!std::isnan(native) && showUnit != nativeUnit)
        if (const auto c = core::convert(native, nativeUnit, showUnit)) shown = *c;

    switch (role) {
        case Qt::DisplayRole:
            return cellNumber(shown, showUnit);
        case kExportRole:
            // The same field the CSV writes for this cell.
            return QString::fromStdString(analysis::formatExportValue(shown, showUnit));
        case kSortRole:
            // A cell with no number sorts to the end either way rather than
            // pretending to be zero.
            return std::isnan(shown) ? QVariant() : QVariant(shown);
        case Qt::TextAlignmentRole:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case Qt::ToolTipRole: {
            if (status != analysis::CellStatus::Ok) return statusExplanation(status);
            // The full readout, with the alternative unit, belongs here rather
            // than in the cell: a grid of "273.15 K (0.00 Cel)" is unreadable,
            // and the header already states the unit in force.
            return formatValueWithUnits(shown, QString::fromStdString(showUnit));
        }
        default:
            return {};
    }
}

QVariant PointProfileModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (section < 0 || section >= columnCount()) return {};

    if (role == Qt::DisplayRole) {
        if (section == 0) return tr("Level");
        const QString unit = QString::fromStdString(core::unitLabel(displayUnitAt(section)));
        const QString name = columnNameAt(section);
        return unit.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, unit);
    }

    if (role == Qt::ToolTipRole) {
        if (section == heightColumn())
            // Say it plainly. Nothing here reads a terrain elevation, so an
            // altitude above ground is a number this table does not have.
            return tr("Mean-sea-level geopotential height, read from '%1'. Not height above "
                      "ground.")
                .arg(QString::fromStdString(profile_.heightSourceVar));
        if (section >= firstValueColumn()) {
            const analysis::ProfileColumn& c =
                profile_.columns[static_cast<std::size_t>(section - firstValueColumn())];
            return QString::fromStdString(c.id);
        }
    }
    return {};
}

}  // namespace met::app
