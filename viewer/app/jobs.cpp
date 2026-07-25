#include "viewer/app/jobs.h"

#include <utility>

#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include "viewer/core/log.h"

namespace met::app {
namespace {

class DecodeRunnable : public QRunnable {
public:
    DecodeRunnable(std::shared_ptr<readers::IDataset> dataset, core::FieldKey key,
                   quint64 generation, QObject* context, std::function<void(DecodeOutcome)> cb)
        : dataset_(std::move(dataset)),
          key_(std::move(key)),
          generation_(generation),
          context_(context),
          cb_(std::move(cb)) {
        setAutoDelete(true);
    }

    void run() override {
        DecodeOutcome outcome;
        outcome.generation = generation_;
        try {
            outcome.field = std::make_shared<core::Field2D>(dataset_->readField(key_));
        } catch (const std::exception& e) {
            core::logf(core::LogLevel::Warn, "decode failed for {}: {}", key_.varName, e.what());
            outcome.error = QString::fromUtf8(e.what());
        }

        // Deliver on the context thread. A destroyed context nulls the QPointer, so
        // the delivery is dropped rather than posted to freed memory.
        QObject* ctx = context_.data();
        if (!ctx) return;
        auto cb = cb_;
        QMetaObject::invokeMethod(
            ctx, [cb, outcome]() mutable { cb(std::move(outcome)); }, Qt::QueuedConnection);
    }

private:
    std::shared_ptr<readers::IDataset> dataset_;
    core::FieldKey key_;
    quint64 generation_;
    QPointer<QObject> context_;
    std::function<void(DecodeOutcome)> cb_;
};

}  // namespace

void submitDecode(QThreadPool& pool, std::shared_ptr<readers::IDataset> dataset, core::FieldKey key,
                  quint64 generation, QObject* context, std::function<void(DecodeOutcome)> cb) {
    pool.start(
        new DecodeRunnable(std::move(dataset), std::move(key), generation, context, std::move(cb)));
}

}  // namespace met::app
