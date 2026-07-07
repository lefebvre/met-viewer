#pragma once

#include <QMainWindow>

namespace met::app {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}  // namespace met::app
