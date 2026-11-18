#include "extraction/ExtractionTask.h"

#include "backends/BackendRegistry.h"
#include "core/Types.h"
#include "logging/Logger.h"

#include <QThread>

namespace eis {

ExtractionTask::ExtractionTask(const QString &jobId, const ExtractionOptions &options,
                               JobControl *control, QObject *parent)
    : QObject(parent), m_jobId(jobId), m_options(options), m_control(control)
{
    setAutoDelete(false);
}

void ExtractionTask::run()
{
    emit started(m_jobId);

    QString format, error;
    auto backend = m_registry.select(m_options.archivePath, m_options.password, &format, &error);
    if (!backend) {
        ExtractionResult result;
        result.backendName = QStringLiteral("none");
        result.error = error.isEmpty() ? QStringLiteral("no backend can handle this archive") : error;
        emit finishedReported(m_jobId, result);
        return;
    }

    auto cb = [this](const ProgressInfo &p) {
        emit progressReported(m_jobId, p);
    };

    ExtractionResult result = backend->extract(m_options, *m_control, cb);
    if (result.backendName.isEmpty())
        result.backendName = backend->name();
    emit finishedReported(m_jobId, result);
}

} // namespace eis
