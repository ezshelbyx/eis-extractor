#pragma once

#include "backends/Backend.h"
#include "backends/BackendRegistry.h"
#include "core/Types.h"

#include <QRunnable>
#include <QString>

namespace eis {

class JobControl;

class ExtractionTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ExtractionTask(const QString &jobId,
                   const ExtractionOptions &options,
                   JobControl *control,
                   QObject *parent = nullptr);

    void run() override;

signals:
    void started(const QString &jobId);
    void progressReported(const QString &jobId, const ProgressInfo &progress);
    void finishedReported(const QString &jobId, const ExtractionResult &result);
    void logReported(const QString &text);

private:
    QString m_jobId;
    ExtractionOptions m_options;
    JobControl *m_control;
    BackendRegistry m_registry;
};

} // namespace eis
