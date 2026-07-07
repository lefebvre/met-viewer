#include "viewer/app/jobs.h"

#include <utility>

#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

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
            outcome.error = QString::fromUtf8(e.what());
        }

        // Deliver on the context thread. If the context is destroyed before the
        // job finishes, the invocation is dropped safely.
        auto cb = cb_;
        QMetaObject::invokeMethod(
            context_, [cb, outcome]() mutable { cb(std::move(outcome)); }, Qt::QueuedConnection);
    }

private:
    std::shared_ptr<readers::IDataset> dataset_;
    core::FieldKey key_;
    quint64 generation_;
    QObject* context_;
    std::function<void(DecodeOutcome)> cb_;
};

}  // namespace

void submitDecode(QThreadPool& pool, std::shared_ptr<readers::IDataset> dataset,
                  core::FieldKey key, quint64 generation, QObject* context,
                  std::function<void(DecodeOutcome)> cb) {
    pool.start(new DecodeRunnable(std::move(dataset), std::move(key), generation, context,
                                  std::move(cb)));
}

}  // namespace met::app
