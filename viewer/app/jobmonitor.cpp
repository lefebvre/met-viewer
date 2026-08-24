#include "viewer/app/jobmonitor.h"

#include <algorithm>
#include <utility>

#include <QProgressBar>
#include <QTimer>

namespace met::app {

JobMonitor::JobMonitor(QProgressBar* bar, QTimer* timer,
                       std::function<void(const QString&)> setStatus, QObject* parent)
    : QObject(parent), bar_(bar), timer_(timer), setStatus_(std::move(setStatus)) {}

void JobMonitor::begin(const QString& text, std::shared_ptr<JobProgress> progress) {
    active_.push_back(std::move(progress));
    if (!text.isEmpty() && setStatus_) setStatus_(text);
    if (bar_) bar_->show();
    if (timer_ && !timer_->isActive()) timer_->start();
    poll();
}

void JobMonitor::end(const std::shared_ptr<JobProgress>& progress) {
    std::erase(active_, progress);
    if (active_.empty()) {
        if (timer_) timer_->stop();
        if (bar_) bar_->hide();
        if (setStatus_) setStatus_(QString());
    } else {
        poll();
    }
}

void JobMonitor::poll() {
    if (active_.empty() || !bar_) return;
    long long done = 0, total = 0;
    bool anyBusy = false;
    for (const auto& j : active_) {
        const int t = j->total.load(std::memory_order_relaxed);
        // total 0 = opaque decode; generating = slabs loaded, now extracting/rendering.
        if (t <= 0 || j->generating.load(std::memory_order_relaxed)) {
            anyBusy = true;
            continue;
        }
        total += t;
        done += std::min<long long>(j->done.load(std::memory_order_relaxed), t);
    }
    if (anyBusy || total <= 0) {
        bar_->setRange(0, 0);  // indeterminate / busy
    } else {
        bar_->setRange(0, static_cast<int>(total));
        bar_->setValue(static_cast<int>(done));
    }
}

}  // namespace met::app
