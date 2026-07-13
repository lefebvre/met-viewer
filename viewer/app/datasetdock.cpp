#include "viewer/app/datasetdock.h"

#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "viewer/core/timeaxis.h"

namespace met::app {
namespace {
// Item data roles for reconstructing a FieldKey from a leaf.
constexpr int kRoleVar = Qt::UserRole + 1;
constexpr int kRoleLevelType = Qt::UserRole + 2;
constexpr int kRoleLevelValue = Qt::UserRole + 3;
constexpr int kRoleTimeEpoch = Qt::UserRole + 4;
constexpr int kRoleMember = Qt::UserRole + 5;
constexpr int kRoleIsLeaf = Qt::UserRole + 6;
}  // namespace

DatasetDock::DatasetDock(QWidget* parent) : QWidget(parent) {
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter…"));
    filter_->setClearButtonEnabled(true);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Variable / Level")});
    tree_->setColumnCount(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(filter_);
    layout->addWidget(tree_);

    connect(filter_, &QLineEdit::textChanged, this, &DatasetDock::filterTree);
    connect(tree_, &QTreeWidget::itemActivated, this, &DatasetDock::onItemActivated);
    connect(tree_, &QTreeWidget::itemClicked, this, &DatasetDock::onItemActivated);
}

void DatasetDock::clear() { tree_->clear(); }

void DatasetDock::setCatalog(const core::DatasetCatalog& catalog) {
    tree_->clear();
    for (const auto& var : catalog.variables()) {
        auto* varItem = new QTreeWidgetItem(tree_);
        QString label = QString::fromStdString(var.varName);
        if (!var.longName.empty())
            label += QStringLiteral(" — ") + QString::fromStdString(var.longName);
        if (!var.units.empty())
            label += QStringLiteral(" [") + QString::fromStdString(var.units) + QStringLiteral("]");
        varItem->setText(0, label);
        varItem->setData(0, kRoleIsLeaf, false);

        const met::core::TimePoint firstTime =
            var.times.empty() ? met::core::TimePoint{} : var.times.front();
        const int member = var.members.empty() ? -1 : var.members.front();

        for (const auto& lvl : var.levels) {
            auto* lvlItem = new QTreeWidgetItem(varItem);
            lvlItem->setText(0, QString::fromStdString(met::core::formatLevel(lvl)));
            lvlItem->setData(0, kRoleIsLeaf, true);
            lvlItem->setData(0, kRoleVar, QString::fromStdString(var.varName));
            lvlItem->setData(0, kRoleLevelType, static_cast<int>(lvl.type));
            lvlItem->setData(0, kRoleLevelValue, lvl.value);
            lvlItem->setData(0, kRoleTimeEpoch, static_cast<qlonglong>(firstTime.epochSeconds));
            lvlItem->setData(0, kRoleMember, member);
        }
    }
    // Load collapsed: top-level variables visible, their levels hidden until the
    // user expands a variable (or a filter match auto-expands it).
    tree_->collapseAll();
    filterTree(filter_->text());
}

void DatasetDock::filterTree(const QString& text) {
    const QString needle = text.trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* varItem = tree_->topLevelItem(i);
        if (needle.isEmpty()) {
            varItem->setHidden(false);
            for (int j = 0; j < varItem->childCount(); ++j) varItem->child(j)->setHidden(false);
            varItem->setExpanded(false);  // restore collapsed default
            continue;
        }
        const bool varMatches =
            varItem->text(0).contains(needle, Qt::CaseInsensitive);
        bool anyLeafMatch = false;
        for (int j = 0; j < varItem->childCount(); ++j) {
            QTreeWidgetItem* leaf = varItem->child(j);
            const bool leafMatch =
                varMatches || leaf->text(0).contains(needle, Qt::CaseInsensitive) ||
                leaf->data(0, kRoleVar).toString().contains(needle, Qt::CaseInsensitive);
            leaf->setHidden(!leafMatch);
            anyLeafMatch = anyLeafMatch || leafMatch;
        }
        const bool visible = varMatches || anyLeafMatch;
        varItem->setHidden(!visible);
        varItem->setExpanded(visible);  // reveal matching levels
    }
}

void DatasetDock::selectField(const QString& varName, const core::VerticalLevel& level) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* varItem = tree_->topLevelItem(i);
        for (int j = 0; j < varItem->childCount(); ++j) {
            QTreeWidgetItem* leaf = varItem->child(j);
            if (leaf->data(0, kRoleVar).toString() != varName) continue;
            const auto type = static_cast<core::VerticalLevel::Type>(
                leaf->data(0, kRoleLevelType).toInt());
            if (type == level.type && leaf->data(0, kRoleLevelValue).toDouble() == level.value) {
                tree_->setCurrentItem(leaf);  // highlights; does not emit itemActivated
                tree_->scrollToItem(leaf);
                return;
            }
        }
    }
}

void DatasetDock::onItemActivated(QTreeWidgetItem* item, int /*column*/) {
    if (!item || !item->data(0, kRoleIsLeaf).toBool()) return;

    core::FieldKey key;
    key.varName = item->data(0, kRoleVar).toString().toStdString();
    key.level.type = static_cast<core::VerticalLevel::Type>(item->data(0, kRoleLevelType).toInt());
    key.level.value = item->data(0, kRoleLevelValue).toDouble();
    key.validTime.epochSeconds = item->data(0, kRoleTimeEpoch).toLongLong();
    key.member = item->data(0, kRoleMember).toInt();
    emit fieldChosen(key);
}

}  // namespace met::app
