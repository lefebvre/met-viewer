#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

#include "viewer/app/jobs.h"

class QProgressBar;
class QTimer;

namespace met::app {

// Aggregates the progress of every background job into one status-bar bar.
//
// Split out of MainWindow because none of it is about the window: it is a small
// state machine over a list of JobProgress handles that decides when the bar is
// visible, whether it reads determinate or busy, and what fraction it shows. The
// window supplies the bar and a way to set the status message; everything else
// lives here, where it can be reasoned about (and tested) without a QMainWindow.
//
// Determinate vs busy is the part worth stating: a job with total <= 0 is an
// opaque decode with no countable steps, and a job flagged `generating` has its
// slabs but is still extracting or rendering. Either makes the *whole* bar busy —
// showing "7 of 9" while an uncountable job runs alongside would be a lie about
// how much is left.
class JobMonitor : public QObject {
    Q_OBJECT
public:
    // `bar` and `timer` are owned by the caller and must outlive this monitor.
    // `setStatus` receives the message to display, or an empty string to clear it.
    JobMonitor(QProgressBar* bar, QTimer* timer, std::function<void(const QString&)> setStatus,
               QObject* parent = nullptr);

    // Start tracking a job. A non-empty `text` becomes the status message.
    void begin(const QString& text, std::shared_ptr<JobProgress> progress);

    // Stop tracking a job. The bar hides and the message clears once the last one
    // ends; otherwise the remaining jobs are re-aggregated immediately.
    void end(const std::shared_ptr<JobProgress>& progress);

    // Recompute the bar from the tracked jobs. Wired to the timer by the caller.
    void poll();

    [[nodiscard]] bool idle() const { return active_.empty(); }
    [[nodiscard]] std::size_t activeCount() const { return active_.size(); }

private:
    QProgressBar* bar_ = nullptr;
    QTimer* timer_ = nullptr;
    std::function<void(const QString&)> setStatus_;
    std::vector<std::shared_ptr<JobProgress>> active_;
};

}  // namespace met::app
