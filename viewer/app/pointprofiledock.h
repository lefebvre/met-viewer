#pragma once

#include <map>
#include <string>
#include <vector>

#include <QSize>
#include <QString>
#include <QWidget>

#include "viewer/analysis/pointprofile.h"
#include "viewer/analysis/profilewriter.h"
#include "viewer/core/geo.h"
#include "viewer/core/timeaxis.h"

class QDoubleSpinBox;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QTableView;
class QTimer;
class QToolButton;

namespace met::app {

class PointProfileModel;

// The point-profile panel: a table of the chosen variables down the vertical
// profile at one picked location, with a column picker, a unit choice, and copy
// and CSV export.
//
// A plain QWidget that knows nothing about docking, following DatasetDock -- the
// window wraps it. Unlike the sounding and cross-section panels this one is a
// singleton the window builds once and re-points, rather than a fresh dock per
// pick: the user re-picks repeatedly, and a dock per click would pile up and
// throw away the column and unit choices they had just made each time.
class PointProfileDock : public QWidget {
    Q_OBJECT
public:
    explicit PointProfileDock(QWidget* parent = nullptr);

    // The columns this dataset can offer. Keeps any previously chosen column that
    // is still available, so reopening a similar file does not silently reset the
    // selection.
    void setChoices(const analysis::ProfileColumnChoices& choices);

    void setProfile(const analysis::PointProfile& profile);
    void clearProfile();

    // Set the point without emitting pointEdited(): the map click that produced
    // it is already being acted on, and re-emitting would loop.
    void setPoint(core::LatLon point);
    [[nodiscard]] core::LatLon point() const;
    [[nodiscard]] bool hasPoint() const { return hasPoint_; }

    [[nodiscard]] std::vector<std::string> selectedColumns() const;
    void setSelectedColumns(const std::vector<std::string>& ids);

    // Show every value whose native unit is `nativeUnit` in `unit` instead. A
    // repaint, not a re-extraction: values are stored natively and converted for
    // display, so this never re-reads a slab.
    void setUnitFor(const std::string& nativeUnit, const std::string& unit);

    // Reorder the table as a header click would. setProfile calls this to put the
    // ground first; it is public so the order can be driven and asserted without
    // synthesizing a click on a header section.
    void sortBy(int column, bool ascending);

    // Provenance for the export header, which only the window knows.
    void setContext(const QString& datasetLabel, core::TimePoint validTime, int member);
    void setCoordPrecision(int digits);
    void setReadEstimate(int reads);
    void setBusy(bool busy);
    // The honest empty state. Empty text clears it and shows the table.
    void setMessage(const QString& text);
    // Explain why there is nothing to read: the file has no vertical axis, or no
    // column is ticked, or the ticked ones have nothing here. Which of those it is
    // is what the reader needs, and only the panel knows.
    void showNothingToRead();

    // What the user is looking at, so tests can assert on it rather than on
    // pixels -- the same reason the plot views expose their hover text. While a
    // message is up the table is hidden, and these report it as empty rather than
    // describing a grid nobody can see.
    // Wide enough for the coordinate row and a few value columns; the panel is
    // mostly columns, and a default-width dock would open showing two of them
    // behind a horizontal scrollbar.
    [[nodiscard]] QSize sizeHint() const override { return {560, 420}; }

    [[nodiscard]] QString message() const;
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] QString headerText(int column) const;
    [[nodiscard]] QString cellText(int row, int column) const;
    [[nodiscard]] QString cellTooltip(int row, int column) const;

    // The table as text, in the order shown. Split from the clipboard and the
    // file dialog so both can be tested without either.
    [[nodiscard]] QString tableAsTsv() const;
    // The selected cells as text, in the order shown, with no header row -- the
    // spreadsheet convention for a copy. Falls back to the whole table (header
    // included, as the Copy button produces it) when nothing is selected.
    [[nodiscard]] QString selectionAsTsv() const;
    // The name the save dialog opens on: the point and the profile's own valid
    // time, so a folder of exports says where and when without opening the files.
    [[nodiscard]] QString suggestedCsvName() const;
    [[nodiscard]] QString tableAsCsv() const;
    // Writes the CSV, reporting why rather than failing silently.
    bool exportCsvTo(const QString& path, QString* error);
    void copyToClipboard();

signals:
    // A latitude/longitude the user typed and committed.
    void pointEdited(core::LatLon point);
    // The column selection changed, debounced so ticking three boxes starts one
    // extraction rather than three.
    void columnsChanged();

private:
    // Asks for a path and writes it, reporting any failure in the panel. Kept
    // beside the table rather than pushed up to the window, which the standing
    // review asks not to grow further.
    void onExport();
    void rebuildColumnMenu();
    void rebuildUnitMenu();
    void applyUnitChoice();
    void loadUnitChoice();
    void saveUnitChoice();
    void updateReadLabel();
    // Profile row indices in the order the view shows them, so an export follows
    // a sort the user just made rather than silently reverting to the stored one.
    [[nodiscard]] analysis::ProfileRowOrder visibleRowOrder() const;
    [[nodiscard]] analysis::ProfileExportInfo exportInfo() const;

    PointProfileModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* readLabel_ = nullptr;
    QToolButton* columnsButton_ = nullptr;
    QToolButton* unitsButton_ = nullptr;
    QMenu* columnsMenu_ = nullptr;
    QMenu* unitsMenu_ = nullptr;
    QDoubleSpinBox* lat_ = nullptr;
    QDoubleSpinBox* lon_ = nullptr;
    QTimer* debounce_ = nullptr;

    analysis::ProfileColumnChoices choices_;
    std::vector<std::string> selected_;
    std::map<std::string, std::string> unitChoice_;
    QString datasetLabel_;
    int readEstimate_ = 0;
    bool hasPoint_ = false;
    bool showingMessage_ = true;
    // Whether a selection has ever been made, by the user or restored from
    // settings. Distinguishes "nothing chosen yet" from "deliberately nothing",
    // so clearing every box does not silently repopulate on the next file.
    bool selectionInitialized_ = false;
};

}  // namespace met::app
