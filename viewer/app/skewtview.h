#pragma once

#include <QWidget>

#include "viewer/analysis/sounding.h"

namespace met::app {

// A skew-T log-p thermodynamic diagram: skewed isotherms, log-pressure isobars,
// dry adiabats, and saturation mixing-ratio lines in the background, with the
// sounding's temperature and dewpoint traces on top.
class SkewTView : public QWidget {
    Q_OBJECT
public:
    explicit SkewTView(QWidget* parent = nullptr);
    void setSounding(const analysis::Sounding& s);

    [[nodiscard]] QSize sizeHint() const override { return {480, 560}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    analysis::Sounding s_;
};

}  // namespace met::app
