#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QSlider;
class QTimer;
class QToolButton;

namespace met::app {

class IconThemer;

// A timeline control: a slider over the loaded time steps with prev/next
// buttons and a label. Emits indexChanged() as the step changes. (Playback is
// added in a later milestone.)
class TimeController : public QWidget {
    Q_OBJECT
public:
    explicit TimeController(QWidget* parent = nullptr);

    // Give the transport buttons themed glyph icons (prev/play/pause/next). The
    // play button's icon tracks the play/pause state and the active theme.
    void setIcons(IconThemer* icons);

    // Set the time-step labels (e.g. ISO strings). Resets the current index to
    // `current`, clamped into range. A single or empty step disables the slider.
    void setSteps(const QStringList& labels, int current = 0);
    [[nodiscard]] int currentIndex() const;
    // Move to `index` (clamped); moves the slider and emits indexChanged. Used by
    // the --time CLI flag to render a specific animation frame headlessly.
    void setCurrentIndex(int index);

    void setFps(int fps);
    [[nodiscard]] bool isPlaying() const;
    void play();   // start playback programmatically
    void pause();  // stop playback

    // Completion handshake: playback is closed-loop. advanceFrame() moves one step
    // and then WAITS; the owner calls frameReady() once that step's field has
    // finished loading, which schedules the next advance. This keeps at most one
    // decode in flight per frame instead of firing on a fixed timer regardless of
    // whether the previous frame loaded. A no-op when not playing.
    void frameReady();

signals:
    void indexChanged(int index);

private slots:
    void onSliderMoved(int value);
    void stepPrev();
    void stepNext();
    void togglePlay();
    void advanceFrame();

private:
    void updateLabel();
    void setPlaying(bool playing);
    void updatePlayIcon();

    IconThemer* icons_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* label_ = nullptr;
    QToolButton* prev_ = nullptr;
    QToolButton* next_ = nullptr;
    QToolButton* play_ = nullptr;
    QTimer* timer_ = nullptr;
    int fps_ = 6;
    bool playing_ = false;  // "playing" state; the single-shot timer is idle between frames
    QStringList labels_;
};

}  // namespace met::app
