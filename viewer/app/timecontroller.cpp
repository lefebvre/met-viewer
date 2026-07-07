#include "viewer/app/timecontroller.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>

namespace met::app {

TimeController::TimeController(QWidget* parent) : QWidget(parent) {
    prev_ = new QToolButton(this);
    prev_->setText("◀");
    prev_->setAutoRepeat(true);
    next_ = new QToolButton(this);
    next_->setText("▶");
    next_->setAutoRepeat(true);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setMinimum(0);
    slider_->setMaximum(0);
    slider_->setEnabled(false);

    label_ = new QLabel(tr("(no time steps)"), this);
    label_->setMinimumWidth(160);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(prev_);
    layout->addWidget(next_);
    layout->addWidget(slider_, 1);
    layout->addWidget(label_);

    connect(slider_, &QSlider::valueChanged, this, &TimeController::onSliderMoved);
    connect(prev_, &QToolButton::clicked, this, &TimeController::stepPrev);
    connect(next_, &QToolButton::clicked, this, &TimeController::stepNext);
}

void TimeController::setSteps(const QStringList& labels, int current) {
    labels_ = labels;
    const int n = static_cast<int>(labels.size());
    const bool many = n > 1;

    // Avoid emitting while reconfiguring range.
    QSignalBlocker block(slider_);
    slider_->setMaximum(n > 0 ? n - 1 : 0);
    slider_->setEnabled(many);
    prev_->setEnabled(many);
    next_->setEnabled(many);
    const int clamped = n > 0 ? std::clamp(current, 0, n - 1) : 0;
    slider_->setValue(clamped);
    updateLabel();
}

int TimeController::currentIndex() const { return slider_->value(); }

void TimeController::onSliderMoved(int value) {
    updateLabel();
    emit indexChanged(value);
}

void TimeController::stepPrev() { slider_->setValue(slider_->value() - 1); }
void TimeController::stepNext() { slider_->setValue(slider_->value() + 1); }

void TimeController::updateLabel() {
    const int i = slider_->value();
    if (i >= 0 && i < labels_.size()) {
        label_->setText(QStringLiteral("%1  (%2/%3)").arg(labels_.at(i)).arg(i + 1).arg(labels_.size()));
    } else {
        label_->setText(tr("(no time steps)"));
    }
}

}  // namespace met::app
