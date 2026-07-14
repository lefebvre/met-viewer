#pragma once

#include <QWidget>

#include "viewer/core/catalog.h"
#include "viewer/core/field.h"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace met::app {

// A tree of variables and their levels. Selecting a level emits fieldChosen()
// with a fully-formed FieldKey (using the variable's first available time for
// M1; a time controller drives time selection in a later milestone).
class DatasetDock : public QWidget {
    Q_OBJECT
public:
    explicit DatasetDock(QWidget* parent = nullptr);

    void setCatalog(const core::DatasetCatalog& catalog);
    void clear();

    // Highlight and scroll to the leaf for `varName` at `level` without emitting
    // fieldChosen(). Used to reflect a programmatic (CLI) selection in the tree.
    void selectField(const QString& varName, const core::VerticalLevel& level);

signals:
    void fieldChosen(const core::FieldKey& key);

private slots:
    void onItemActivated(QTreeWidgetItem* item, int column);
    void filterTree(const QString& text);

private:
    QLineEdit* filter_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

}  // namespace met::app
