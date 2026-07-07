#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QString>
#include <QtGlobal>

#include "viewer/core/field.h"
#include "viewer/readers/ireader.h"

class QThreadPool;

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

}  // namespace met::app
