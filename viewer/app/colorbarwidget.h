#pragma once

#include <memory>

#include <QWidget>

#include "viewer/render/colormap.h"

namespace met::app {

// A vertical colorbar rendering a Colormap's gradient with min/mid/max labels
// and an optional units suffix. Driven by setColormap().
class ColorbarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorbarWidget(QWidget* parent = nullptr);

    void setColormap(const render::Colormap& cmap);
    void setUnits(const QString& units);
    void clear();

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::unique_ptr<render::Colormap> cmap_;
    QString units_;
};

}  // namespace met::app
