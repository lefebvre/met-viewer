#include "viewer/app/pointprofiledock.h"

#include <algorithm>
#include <cctype>

#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "viewer/app/pointprofilemodel.h"
#include "viewer/core/timeaxis.h"
#include "viewer/core/units.h"

namespace met::app {
namespace {

// Ticking three boxes must start one extraction, not three. 200 ms is below what
// reads as lag and above a fast run down a menu.
constexpr int kColumnDebounceMs = 200;

// A unit string can contain '/', which QSettings reads as a group separator, so
// "m/s" would silently become a group named "m" holding a key "s". Swapping it
// for a character that cannot appear in a unit keeps one flat key per unit.
// What to show before the user has chosen anything. An empty table on the first
// pick would be honest and useless, so this opens on the quantities a site
// readout is usually wanted for, and falls back to whatever the file leads with
// when it carries none of them.
std::vector<std::string> defaultColumns(const analysis::ProfileColumnChoices& choices) {
    static const std::vector<std::string> kPreferred = {"t", "temp", "tmp", "r", "rh", "q"};
    std::vector<std::string> out;
    for (const analysis::ProfileColumn& c : choices.available) {
        std::string lower = c.id;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (std::find(kPreferred.begin(), kPreferred.end(), lower) != kPreferred.end())
            out.push_back(c.id);
    }
    if (out.empty())
        for (const analysis::ProfileColumn& c : choices.available) {
            if (out.size() >= 2) break;
            out.push_back(c.id);
        }
    if (choices.windAvailable) {
        out.push_back(analysis::kWindSpeedId);
        out.push_back(analysis::kWindDirectionId);
    }
    return out;
}

QString unitSettingsKey(const std::string& nativeUnit) {
    QString key = QString::fromStdString(nativeUnit);
    key.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("pointProfile/units/") + key;
}

}  // namespace

PointProfileDock::PointProfileDock(QWidget* parent) : QWidget(parent) {
    model_ = new PointProfileModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    // Sort on the raw number, never the displayed string: 1000 would otherwise
    // sort before 850.
    proxy_->setSortRole(PointProfileModel::kSortRole);

    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    // Deliberately no alternating row colours: the style's default AlternateBase
    // under the dark scheme is a saturated red that reads as an error state. The
    // table is a few dozen rows and tracks fine without banding.
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    message_ = new QLabel(this);
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setEnabled(false);  // reads as secondary in either theme

    columnsButton_ = new QToolButton(this);
    columnsButton_->setText(tr("Columns"));
    columnsButton_->setPopupMode(QToolButton::InstantPopup);
    columnsMenu_ = new QMenu(columnsButton_);
    columnsButton_->setMenu(columnsMenu_);

    unitsButton_ = new QToolButton(this);
    unitsButton_->setText(tr("Units"));
    unitsButton_->setPopupMode(QToolButton::InstantPopup);
    unitsMenu_ = new QMenu(unitsButton_);
    unitsButton_->setMenu(unitsMenu_);

    readLabel_ = new QLabel(this);
    readLabel_->setEnabled(false);
    // The read cost is shown rather than capped: the column set is the user's to
    // choose, and a silent cap would truncate a legitimate request. Seeing "242
    // reads" before ticking a fourth box is what a cap was meant to achieve.
    readLabel_->setToolTip(tr("Slabs this selection reads each time the table updates."));

    auto* copy = new QPushButton(tr("Copy"), this);
    auto* exportCsv = new QPushButton(tr("Export CSV…"), this);

    lat_ = new QDoubleSpinBox(this);
    lat_->setRange(-90.0, 90.0);
    lat_->setDecimals(4);
    lat_->setSuffix(QStringLiteral("°"));
    lon_ = new QDoubleSpinBox(this);
    lon_->setRange(-180.0, 180.0);
    lon_->setDecimals(4);
    lon_->setSuffix(QStringLiteral("°"));
    auto* go = new QPushButton(tr("Go"), this);

    auto* tools = new QHBoxLayout;
    tools->addWidget(columnsButton_);
    tools->addWidget(unitsButton_);
    tools->addWidget(readLabel_);
    tools->addStretch(1);
    tools->addWidget(copy);
    tools->addWidget(exportCsv);

    auto* coords = new QHBoxLayout;
    coords->addWidget(new QLabel(tr("Lat"), this));
    coords->addWidget(lat_);
    coords->addWidget(new QLabel(tr("Lon"), this));
    coords->addWidget(lon_);
    coords->addWidget(go);
    coords->addStretch(1);

    auto* root = new QVBoxLayout(this);
    root->addLayout(tools);
    root->addLayout(coords);
    root->addWidget(table_, 1);
    root->addWidget(message_, 1);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(kColumnDebounceMs);
    connect(debounce_, &QTimer::timeout, this, &PointProfileDock::columnsChanged);

    // editingFinished, never valueChanged: the latter fires on every keystroke
    // and every arrow-key press, which would launch an extraction per digit.
    const auto commit = [this] {
        hasPoint_ = true;
        emit pointEdited(point());
    };
    connect(go, &QPushButton::clicked, this, commit);
    connect(lat_, &QDoubleSpinBox::editingFinished, this, commit);
    connect(lon_, &QDoubleSpinBox::editingFinished, this, commit);

    connect(copy, &QPushButton::clicked, this, &PointProfileDock::copyToClipboard);
    connect(exportCsv, &QPushButton::clicked, this, &PointProfileDock::onExport);

    loadUnitChoice();
    setMessage(tr("Pick a point on the map, or type coordinates above."));
}

void PointProfileDock::setChoices(const analysis::ProfileColumnChoices& choices) {
    choices_ = choices;
    // Keep what is still on offer. A file swap should not silently reset a
    // selection the user built, but it also must not carry over a column this
    // dataset has never heard of.
    std::vector<std::string> kept;
    for (const std::string& id : selected_) {
        const bool derived = id == analysis::kWindSpeedId || id == analysis::kWindDirectionId;
        if (derived) {
            if (choices.windAvailable) kept.push_back(id);
            continue;
        }
        for (const analysis::ProfileColumn& c : choices.available)
            if (c.id == id) {
                kept.push_back(id);
                break;
            }
    }
    selected_ = std::move(kept);
    // Only once a dataset actually offers something. setChoices also runs with an
    // empty set before any file is open, and treating that as "the user chose
    // nothing" would leave the first real dataset with no columns at all.
    if (!selectionInitialized_ && !choices.available.empty()) {
        selected_ = defaultColumns(choices);
        selectionInitialized_ = true;
    }
    rebuildColumnMenu();
}

void PointProfileDock::rebuildColumnMenu() {
    columnsMenu_->clear();

    const auto addToggle = [this](const QString& text, const std::string& id) {
        QAction* act = columnsMenu_->addAction(text);
        act->setCheckable(true);
        act->setChecked(std::find(selected_.begin(), selected_.end(), id) != selected_.end());
        connect(act, &QAction::toggled, this, [this, id](bool on) {
            const auto it = std::find(selected_.begin(), selected_.end(), id);
            if (on && it == selected_.end()) selected_.push_back(id);
            else if (!on && it != selected_.end()) selected_.erase(it);
            debounce_->start();
        });
    };

    if (choices_.windAvailable) {
        columnsMenu_->addSection(tr("Derived"));
        addToggle(tr("Wind speed"), analysis::kWindSpeedId);
        addToggle(tr("Wind direction"), analysis::kWindDirectionId);
    }

    if (!choices_.available.empty()) {
        columnsMenu_->addSection(tr("Variables"));
        for (const analysis::ProfileColumn& c : choices_.available) {
            const QString name = QString::fromStdString(c.longName.empty() ? c.id : c.longName);
            const QString unit = QString::fromStdString(core::unitLabel(c.units));
            addToggle(unit.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, unit), c.id);
        }
    }

    // Shown disabled rather than dropped: a reader who can see a field on the map
    // and not in this menu would otherwise be left guessing why.
    if (!choices_.unavailable.empty()) {
        columnsMenu_->addSection(tr("Not on this vertical axis"));
        for (const analysis::ProfileColumn& c : choices_.unavailable) {
            QAction* act = columnsMenu_->addAction(
                QString::fromStdString(c.longName.empty() ? c.id : c.longName));
            act->setEnabled(false);
            act->setToolTip(tr("Only one level in this file, so it has no vertical profile."));
        }
    }

    if (columnsMenu_->isEmpty())
        columnsMenu_->addAction(tr("No variables with a vertical profile"))->setEnabled(false);
}

void PointProfileDock::rebuildUnitMenu() {
    unitsMenu_->clear();
    bool any = false;
    for (const std::string& native : model_->nativeUnitsInUse()) {
        const std::vector<std::string> alts = core::alternativeUnits(native);
        if (alts.size() < 2) continue;  // nothing to choose between
        any = true;
        QMenu* sub = unitsMenu_->addMenu(QString::fromStdString(core::unitLabel(native)));
        auto* group = new QActionGroup(sub);
        group->setExclusive(true);
        const auto chosen = unitChoice_.find(native);
        for (const std::string& alt : alts) {
            QAction* act = sub->addAction(QString::fromStdString(core::unitLabel(alt)));
            act->setCheckable(true);
            act->setChecked(chosen == unitChoice_.end() ? alt == native : chosen->second == alt);
            group->addAction(act);
            connect(act, &QAction::triggered, this,
                    [this, native, alt] { setUnitFor(native, alt); });
        }
    }
    if (!any) unitsMenu_->addAction(tr("No alternative units"))->setEnabled(false);
    unitsButton_->setEnabled(any);
}

void PointProfileDock::applyUnitChoice() {
    // A repaint, not a job: values are stored natively and converted for display,
    // so changing a unit never re-reads a slab.
    model_->setUnitChoice(unitChoice_);
    rebuildUnitMenu();
}

void PointProfileDock::loadUnitChoice() {
    QSettings s;
    // Only the units this build knows how to convert are restored, so a stale
    // setting cannot pin a column to a unit convert() would refuse.
    for (const char* native : {"K", "Pa", "hPa", "m/s", "gpm", "dam", "m2/s2", "kg/kg", "m"}) {
        const QString value = s.value(unitSettingsKey(native)).toString();
        if (value.isEmpty()) continue;
        const std::vector<std::string> alts = core::alternativeUnits(native);
        const std::string want = value.toStdString();
        if (std::find(alts.begin(), alts.end(), want) != alts.end()) unitChoice_[native] = want;
    }
    model_->setUnitChoice(unitChoice_);
}

void PointProfileDock::saveUnitChoice() {
    QSettings s;
    for (const auto& [native, chosen] : unitChoice_)
        s.setValue(unitSettingsKey(native), QString::fromStdString(chosen));
}

void PointProfileDock::setProfile(const analysis::PointProfile& profile) {
    model_->setProfile(profile);
    rebuildUnitMenu();

    if (profile.levels.empty()) {
        setMessage(tr("This dataset has no vertical profile to table."));
        return;
    }
    if (!profile.pointInDomain) {
        setMessage(tr("That point lies outside this dataset's grid."));
        return;
    }
    setMessage({});

    // On an isobaric axis the level label and the pressure column say the same
    // thing, so showing both wastes the width the actual values need. The label
    // stays for every other axis, where it is the only honest name for a row --
    // "model level 20" has no pressure of its own until the pres field is read.
    table_->setColumnHidden(0, profile.levelType == core::VerticalLevel::Type::PressureHPa);

    // Ground first. The profile already arrives in that order, but the view sorts
    // independently, so the initial sort has to say it too. Altitude ascending
    // and pressure descending are the same order; whichever coordinate the file
    // supports is the one to sort on.
    const int heightCol = model_->heightColumn();
    if (heightCol >= 0) sortBy(heightCol, /*ascending=*/true);
    else sortBy(model_->pressureColumn(), /*ascending=*/false);
}

void PointProfileDock::showNothingToRead() {
    if (choices_.levelType == core::VerticalLevel::Type::Unknown)
        setMessage(tr("This dataset has no vertical profile to table."));
    else if (selected_.empty()) setMessage(tr("Choose one or more columns to table."));
    else setMessage(tr("None of the chosen columns has data on this dataset's vertical axis."));
}

void PointProfileDock::clearProfile() {
    model_->clearProfile();
    setMessage(tr("Pick a point on the map, or type coordinates above."));
}

void PointProfileDock::setPoint(core::LatLon p) {
    const QSignalBlocker blockLat(lat_);
    const QSignalBlocker blockLon(lon_);
    lat_->setValue(p.lat);
    lon_->setValue(core::wrapLon180(p.lon));
    hasPoint_ = true;
}

core::LatLon PointProfileDock::point() const { return {lat_->value(), lon_->value()}; }

std::vector<std::string> PointProfileDock::selectedColumns() const { return selected_; }

void PointProfileDock::setSelectedColumns(const std::vector<std::string>& ids) {
    selected_ = ids;
    selectionInitialized_ = true;
    rebuildColumnMenu();
}

void PointProfileDock::setUnitFor(const std::string& nativeUnit, const std::string& unit) {
    unitChoice_[nativeUnit] = unit;
    saveUnitChoice();
    applyUnitChoice();
}

void PointProfileDock::sortBy(int column, bool ascending) {
    table_->sortByColumn(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void PointProfileDock::setContext(const QString& datasetLabel, core::TimePoint, int) {
    datasetLabel_ = datasetLabel;
}

void PointProfileDock::setCoordPrecision(int digits) {
    lat_->setDecimals(std::max(2, digits + 2));
    lon_->setDecimals(std::max(2, digits + 2));
}

void PointProfileDock::setReadEstimate(int reads) {
    readEstimate_ = reads;
    updateReadLabel();
}

void PointProfileDock::updateReadLabel() {
    if (readEstimate_ <= 0) {
        readLabel_->clear();
        return;
    }
    readLabel_->setText(tr("%n slab read(s)", nullptr, readEstimate_));
}

void PointProfileDock::setBusy(bool busy) { table_->setEnabled(!busy); }

void PointProfileDock::setMessage(const QString& text) {
    showingMessage_ = !text.isEmpty();
    message_->setText(text);
    message_->setVisible(showingMessage_);
    table_->setVisible(!showingMessage_);
}

QString PointProfileDock::message() const { return showingMessage_ ? message_->text() : QString(); }

int PointProfileDock::rowCount() const { return showingMessage_ ? 0 : proxy_->rowCount(); }
int PointProfileDock::columnCount() const { return showingMessage_ ? 0 : proxy_->columnCount(); }

QString PointProfileDock::headerText(int column) const {
    return proxy_->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
}

QString PointProfileDock::cellText(int row, int column) const {
    return proxy_->index(row, column).data(Qt::DisplayRole).toString();
}

QString PointProfileDock::cellTooltip(int row, int column) const {
    return proxy_->index(row, column).data(Qt::ToolTipRole).toString();
}

analysis::ProfileRowOrder PointProfileDock::visibleRowOrder() const {
    analysis::ProfileRowOrder order;
    order.reserve(static_cast<std::size_t>(proxy_->rowCount()));
    for (int r = 0; r < proxy_->rowCount(); ++r) {
        const QVariant idx = proxy_->index(r, 0).data(PointProfileModel::kRowIndexRole);
        if (idx.isValid()) order.push_back(static_cast<std::size_t>(idx.toInt()));
    }
    return order;
}

analysis::ProfileExportInfo PointProfileDock::exportInfo() const {
    analysis::ProfileExportInfo info;
    info.datasetLabel = datasetLabel_.toStdString();
    return info;
}

QString PointProfileDock::tableAsTsv() const {
    return QString::fromStdString(
        analysis::profileToTsv(model_->profile(), model_->exportUnits(), visibleRowOrder()));
}

QString PointProfileDock::tableAsCsv() const {
    return QString::fromStdString(analysis::profileToCsv(model_->profile(), exportInfo(),
                                                         model_->exportUnits(), visibleRowOrder()));
}

void PointProfileDock::copyToClipboard() {
    if (QClipboard* clip = QGuiApplication::clipboard()) clip->setText(tableAsTsv());
}

void PointProfileDock::onExport() {
    if (model_->profile().levels.empty()) return;
    const core::LatLon p = point();
    // A name that says what the file is without opening it.
    const QString suggested =
        QStringLiteral("profile_%1_%2.csv")
            .arg(QString::number(p.lat, 'f', 2), QString::number(p.lon, 'f', 2));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export point profile"), suggested,
                                                      tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!exportCsvTo(path, &error)) setMessage(tr("Could not write %1: %2").arg(path, error));
}

bool PointProfileDock::exportCsvTo(const QString& path, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = tableAsCsv().toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        // Say why. A half-written or absent export that reported success is worse
        // than no export at all.
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

}  // namespace met::app
