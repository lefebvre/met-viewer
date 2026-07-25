#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRunnable>
#include <QString>
#include <QThreadPool>
#include <QtGlobal>

#include "viewer/core/field.h"
#include "viewer/readers/ireader.h"

namespace met::app {

// Progress of a background job. The worker thread bumps `done` (one per slab read)
// while the GUI thread polls both counters to drive a progress bar. `total == 0`
// means the amount of work is unknown up front (e.g. a single opaque decode) and
// the bar should show a busy/indeterminate animation instead of a percentage.
// `generating` is set once the slabs are loaded and the (unmeasured) plot
// extraction/rendering begins, so the bar can switch from a percentage to a busy
// animation for that phase instead of sitting at 100% until the plot appears.
struct JobProgress {
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<bool> generating{false};
};

// Result of a background decode. `field` is null when `error` is set. The
// `generation` echoes the value passed to submitDecode so the receiver can drop
// stale results (e.g. the user moved on to another field).
struct DecodeOutcome {
    quint64 generation = 0;
    std::shared_ptr<core::Field2D> field;
    QString error;
};

// Decode one field on `pool`. `cb` runs on `context`'s thread (typically the GUI
// thread) via a queued invocation, so it may touch widgets. The dataset is held
// by shared_ptr for the duration of the job. If `context` is destroyed while the
// job runs, the result is discarded and `cb` never runs — see the QPointer note
// on submitCompute.
void submitDecode(QThreadPool& pool, std::shared_ptr<readers::IDataset> dataset, core::FieldKey key,
                  quint64 generation, QObject* context, std::function<void(DecodeOutcome)> cb);

// Run `compute` on `pool`; deliver its result to `done` on `context`'s thread via
// a queued invocation (so `done` may touch widgets). Use for heavier multi-slab
// extractions (cross-section / sounding / time series) that must not block the UI
// thread the way a synchronous readField loop does.
//
// The context is held by QPointer, not a raw pointer. Qt drops a queued event
// whose receiver is destroyed *after* the event was posted, but posting to an
// already-destroyed QObject is a use-after-free — invokeMethod dereferences the
// receiver to find its thread. QPointer nulls on destruction, so a job that
// outlives its context simply drops the delivery. (QPointer is only safe to read
// from the GUI thread; the check below is a best-effort guard for the common case
// where the context is destroyed long before the job finishes. Contexts must
// still outlive the pool — MainWindow owns its QThreadPool, whose destructor
// waits for outstanding jobs.)
template <typename T>
void submitCompute(QThreadPool& pool, QObject* context, std::function<T()> compute,
                   std::function<void(T)> done) {
    class ComputeRunnable : public QRunnable {
    public:
        ComputeRunnable(QObject* ctx, std::function<T()> f, std::function<void(T)> d)
            : ctx_(ctx), compute_(std::move(f)), done_(std::move(d)) {
            setAutoDelete(true);
        }
        void run() override {
            T result = compute_();
            QObject* ctx = ctx_.data();
            if (!ctx) return;  // context went away: nothing to deliver to
            std::function<void(T)> d = done_;
            QMetaObject::invokeMethod(
                ctx, [d, result = std::move(result)]() mutable { d(std::move(result)); },
                Qt::QueuedConnection);
        }

    private:
        QPointer<QObject> ctx_;
        std::function<T()> compute_;
        std::function<void(T)> done_;
    };
    pool.start(new ComputeRunnable(context, std::move(compute), std::move(done)));
}

}  // namespace met::app
