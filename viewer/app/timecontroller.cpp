#include "viewer/app/timecontroller.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

#include "viewer/app/icons.h"
#include "viewer/app/theme.h"

namespace met::app {

TimeController::TimeController(QWidget* parent) : QWidget(parent) {
    prev_ = new QToolButton(this);
    prev_->setText("◀");
    play_ = new QToolButton(this);
    play_->setText("▶");  // play glyph; becomes ⏸ while playing
    next_ = new QToolButton(this);
    next_->setText("▶▏");

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setMinimum(0);
    slider_->setMaximum(0);
    slider_->setEnabled(false);

    label_ = new QLabel(tr("(no time steps)"), this);
    label_->setMinimumWidth(160);

    timer_ = new QTimer(this);
    // Single-shot: each frame arms exactly one advance, and the next is only armed
    // once the current frame reports ready (frameReady()) — closed-loop playback.
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &TimeController::advanceFrame);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(prev_);
    layout->addWidget(play_);
    layout->addWidget(next_);
    layout->addWidget(slider_, 1);
    layout->addWidget(label_);

    connect(slider_, &QSlider::valueChanged, this, &TimeController::onSliderMoved);
    connect(prev_, &QToolButton::clicked, this, &TimeController::stepPrev);
    connect(next_, &QToolButton::clicked, this, &TimeController::stepNext);
    connect(play_, &QToolButton::clicked, this, &TimeController::togglePlay);
}

void TimeController::setIcons(IconThemer* icons) {
    icons_ = icons;
    // Swap the unicode glyphs for themed icons; keep accessible text for
    // tooltips / accessibility / tests.
    for (auto* b : {prev_, play_, next_}) b->setText(QString());
    prev_->setToolTip(tr("Previous step"));
    prev_->setAccessibleName(tr("Previous step"));
    next_->setToolTip(tr("Next step"));
    next_->setAccessibleName(tr("Next step"));
    play_->setToolTip(tr("Play / pause"));
    play_->setAccessibleName(tr("Play/pause"));
    icons_->applyButton(prev_, "anim-prev");
    icons_->applyButton(next_, "anim-next");
    updatePlayIcon();
    // The play button's glyph depends on state, so refresh it on theme change
    // ourselves rather than registering a fixed token.
    connect(icons_->theme(), &ThemeManager::effectiveSchemeChanged, this,
            [this](bool) { updatePlayIcon(); });
}

void TimeController::updatePlayIcon() {
    if (icons_) play_->setIcon(icons_->iconFor(isPlaying() ? "anim-pause" : "anim-play"));
}

void TimeController::setFps(int fps) {
    fps_ = std::clamp(fps, 1, 60);
    if (timer_->isActive()) timer_->start(1000 / fps_);
}

// A single-shot timer is idle *between* frames (while a decode is in flight), so
// "playing" is tracked explicitly rather than inferred from the timer.
bool TimeController::isPlaying() const { return playing_; }

void TimeController::setPlaying(bool playing) {
    playing_ = playing && slider_->maximum() > 0;
    if (playing_) {
        timer_->start(1000 / fps_);  // arm the first advance
    } else {
        timer_->stop();
    }
    if (icons_) updatePlayIcon();
    else play_->setText(playing_ ? "⏸" : "▶");  // glyph fallback without icons
}

void TimeController::togglePlay() { setPlaying(!playing_); }

void TimeController::play() { setPlaying(true); }
void TimeController::pause() { setPlaying(false); }

void TimeController::advanceFrame() {
    if (!playing_) return;
    if (slider_->maximum() <= 0) { setPlaying(false); return; }
    const int next = (slider_->value() + 1) % (slider_->maximum() + 1);  // loop
    // Moves the slider (emits indexChanged → decode). The timer is NOT re-armed here;
    // the owner calls frameReady() once this frame's field is ready, arming the next.
    slider_->setValue(next);
}

void TimeController::frameReady() {
    if (playing_) timer_->start(1000 / fps_);  // schedule the next advance
}

void TimeController::setSteps(const QStringList& labels, int current) {
    labels_ = labels;
    const int n = static_cast<int>(labels.size());
    const bool many = n > 1;

    // Avoid emitting while reconfiguring range.
    if (!many) setPlaying(false);
    QSignalBlocker block(slider_);
    slider_->setMaximum(n > 0 ? n - 1 : 0);
    slider_->setEnabled(many);
    prev_->setEnabled(many);
    next_->setEnabled(many);
    play_->setEnabled(many);
    const int clamped = n > 0 ? std::clamp(current, 0, n - 1) : 0;
    slider_->setValue(clamped);
    updateLabel();
}

int TimeController::currentIndex() const { return slider_->value(); }

void TimeController::setCurrentIndex(int index) {
    slider_->setValue(std::clamp(index, 0, slider_->maximum()));  // emits valueChanged if it moves
}

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
