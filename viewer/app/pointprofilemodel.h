#pragma once

#include <map>
#include <string>
#include <vector>

#include <QAbstractTableModel>
#include <QString>

#include "viewer/analysis/pointprofile.h"
#include "viewer/analysis/profilewriter.h"

namespace met::app {

// A table view of one analysis::PointProfile: a row per vertical level, a column
// per selected variable, behind the two fixed coordinate columns.
//
// A model rather than a QTableWidget, which is what the rest of this app's item
// views use, for two reasons. The table is rebuilt on every time step while
// playback runs, and allocating an item per cell per frame is the cost a model
// exists to avoid. More importantly it keeps the PointProfile as the single
// source of truth: the model formats for display, and the writer serializes the
// same profile for export, so "the cell says 273.15 K (0.00 Cel), the CSV says
// 273.15" is a property of the design rather than a rule someone has to
// remember.
//
// Sorting is left to a QSortFilterProxyModel over kSortRole, which carries the
// raw number. Sorting on the displayed string would order 1000 before 850.
class PointProfileModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // The numeric value behind a cell, for a proxy to sort on.
    static constexpr int kSortRole = Qt::UserRole + 1;
    // The cell's row index in the profile, so a sorted view can be mapped back to
    // the profile order an export needs.
    static constexpr int kRowIndexRole = Qt::UserRole + 2;

    explicit PointProfileModel(QObject* parent = nullptr);

    void setProfile(const analysis::PointProfile& profile);
    [[nodiscard]] const analysis::PointProfile& profile() const { return profile_; }
    void clearProfile();

    // Display unit per *native* unit, not per column: picking knots once applies
    // to every speed in the table, which is what a reader means by the choice.
    // Values are always stored natively and converted on the way out, so this
    // never invalidates the profile and never triggers a re-extraction.
    void setUnitChoice(const std::map<std::string, std::string>& choice);
    [[nodiscard]] const std::map<std::string, std::string>& unitChoice() const {
        return unitChoice_;
    }

    // The native units this table is showing, so a menu can offer alternatives
    // for each. Includes the two coordinate columns.
    [[nodiscard]] std::vector<std::string> nativeUnitsInUse() const;

    // The chosen units in the shape the writer takes, so the export cannot use a
    // different unit from the one on screen.
    [[nodiscard]] analysis::ProfileUnits exportUnits() const;

    // Which model column carries the altitude, or -1 when the file has no height
    // field. The view sorts on this by default: a site table is read by altitude.
    [[nodiscard]] int heightColumn() const;
    [[nodiscard]] int pressureColumn() const { return 1; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;

private:
    // The unit a model column is displayed in, and the native unit it converts
    // from. Column 0 (the level label) has neither.
    [[nodiscard]] std::string nativeUnitAt(int column) const;
    [[nodiscard]] std::string displayUnitAt(int column) const;
    [[nodiscard]] int firstValueColumn() const { return heightColumn() >= 0 ? 3 : 2; }

    analysis::PointProfile profile_;
    std::map<std::string, std::string> unitChoice_;
};

}  // namespace met::app
