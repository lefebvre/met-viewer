#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QRunnable>
#include <QString>
#include <QThreadPool>
#include <QtGlobal>

#include "viewer/core/field.h"
#include "viewer/readers/ireader.h"

namespace met::app {

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
// by shared_ptr for the duration of the job.
void submitDecode(QThreadPool& pool, std::shared_ptr<readers::IDataset> dataset,
                  core::FieldKey key, quint64 generation, QObject* context,
                  std::function<void(DecodeOutcome)> cb);

// Run `compute` on `pool`; deliver its result to `done` on `context`'s thread via
// a queued invocation (so `done` may touch widgets). If `context` is destroyed
// before the job finishes, the delivery is dropped. Use for heavier multi-slab
// extractions (cross-section / sounding / time series) that must not block the UI
// thread the way a synchronous readField loop does.
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
            std::function<void(T)> d = done_;
            QMetaObject::invokeMethod(
                ctx_, [d, result = std::move(result)]() mutable { d(std::move(result)); },
                Qt::QueuedConnection);
        }

    private:
        QObject* ctx_;
        std::function<T()> compute_;
        std::function<void(T)> done_;
    };
    pool.start(new ComputeRunnable(context, std::move(compute), std::move(done)));
}

}  // namespace met::app
