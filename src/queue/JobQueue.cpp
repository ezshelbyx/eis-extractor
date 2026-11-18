#include "queue/JobQueue.h"

#include "core/Types.h"
#include "logging/Logger.h"
#include "settings/AppSettings.h"

#include <QDateTime>

#include <QUuid>

namespace eis {

JobQueue &JobQueue::instance()
{
    static JobQueue queue;
    return queue;
}

JobQueue::JobQueue(QObject *parent)
    : QObject(parent)
{
    m_maxConcurrent = AppSettings::instance().maxConcurrent();
    m_pool.setMaxThreadCount(m_maxConcurrent);
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        setMaxConcurrent(AppSettings::instance().maxConcurrent());
    });
}

void JobQueue::setMaxConcurrent(int value)
{
    m_maxConcurrent = qBound(1, value, 4);
    m_pool.setMaxThreadCount(m_maxConcurrent);
    tryStart();
}

QString JobQueue::enqueue(const ExtractionOptions &options)
{
    Entry entry;
    entry.record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.record.options = options;
    entry.record.state = JobState::Queued;
    entry.record.queuedAt = QDateTime::currentDateTime();
    entry.control = std::make_shared<JobControl>();

    QMutexLocker locker(&m_mutex);
    m_entries.append(entry);
    const QString id = m_entries.last().record.id;
    const JobRecord snapshot = m_entries.last().record;
    locker.unlock();

    emit jobAdded(snapshot);
    tryStart();
    return id;
}

JobRecord JobQueue::job(const QString &jobId) const
{
    QMutexLocker locker(&m_mutex);
    for (const Entry &e : m_entries) {
        if (e.record.id == jobId)
            return e.record;
    }
    return JobRecord();
}

QVector<JobRecord> JobQueue::jobs() const
{
    QMutexLocker locker(&m_mutex);
    QVector<JobRecord> result;
    for (const Entry &e : m_entries)
        result.append(e.record);
    return result;
}

void JobQueue::cancel(const QString &jobId)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e)
        return;
    if (e->record.state == JobState::Queued) {
        e->record.state = JobState::Cancelled;
        e->record.finishedAt = QDateTime::currentDateTime();
        const JobRecord snapshot = e->record;
        locker.unlock();
        emit jobUpdated(snapshot);
        tryStart();
        return;
    }
    if (e->record.state == JobState::Running || e->record.state == JobState::Paused) {
        if (e->control)
            e->control->cancel.store(true);
        if (e->record.state == JobState::Paused) {
            e->record.state = JobState::Running;
            if (e->control)
                e->control->pause.store(false);
        }
    }
    locker.unlock();
}

void JobQueue::pause(const QString &jobId)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e || e->record.state != JobState::Running)
        return;
    if (e->control)
        e->control->pause.store(true);
    e->record.state = JobState::Paused;
    const JobRecord snapshot = e->record;
    locker.unlock();
    emit jobUpdated(snapshot);
}

void JobQueue::resume(const QString &jobId)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e || e->record.state != JobState::Paused)
        return;
    if (e->control)
        e->control->pause.store(false);
    e->record.state = JobState::Running;
    const JobRecord snapshot = e->record;
    locker.unlock();
    emit jobUpdated(snapshot);
}

void JobQueue::retry(const QString &jobId)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e)
        return;
    if (e->record.state != JobState::Failed && e->record.state != JobState::Cancelled)
        return;
    e->record.state = JobState::Queued;
    e->record.progress = ProgressInfo();
    e->record.result = ExtractionResult();
    e->record.startedAt = QDateTime();
    e->record.finishedAt = QDateTime();
    ++e->record.attempt;
    e->control = std::make_shared<JobControl>();
    const JobRecord snapshot = e->record;
    locker.unlock();
    emit jobUpdated(snapshot);
    tryStart();
}

void JobQueue::tryStart()
{
    QMutexLocker locker(&m_mutex);
    int running = 0;
    for (const Entry &e : m_entries)
        if (e.record.state == JobState::Running || e.record.state == JobState::Paused)
            ++running;

    bool startedAny = false;
    for (Entry &e : m_entries) {
        if (running >= m_maxConcurrent)
            break;
        if (e.record.state != JobState::Queued)
            continue;
        if (!e.control)
            e.control = std::make_shared<JobControl>();

        e.record.state = JobState::Running;
        e.record.startedAt = QDateTime::currentDateTime();
        ExtractionTask *task = new ExtractionTask(e.record.id, e.record.options, e.control.get(), this);
        e.task = task;

        connect(task, &ExtractionTask::started, this, &JobQueue::onStarted);
        connect(task, &ExtractionTask::progressReported, this, &JobQueue::onProgress);
        connect(task, &ExtractionTask::finishedReported, this, &JobQueue::onFinished);
        connect(task, &ExtractionTask::logReported, this, &JobQueue::onLog);

        m_pool.start(task);
        ++running;
        startedAny = true;
    }
    if (startedAny) {
        // emit updates for started jobs
    }
    // Emit jobUpdated for each running job that just started? We'll emit in onStarted.
}

void JobQueue::onStarted(const QString &jobId)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e)
        return;
    const JobRecord snapshot = e->record;
    locker.unlock();
    emit jobUpdated(snapshot);
}

void JobQueue::onProgress(const QString &jobId, const ProgressInfo &progress)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e)
        return;
    e->record.progress = progress;
    const JobRecord snapshot = e->record;
    locker.unlock();
    emit progressUpdated(jobId, progress);
    emit jobUpdated(snapshot);
}

void JobQueue::onFinished(const QString &jobId, const ExtractionResult &result)
{
    QMutexLocker locker(&m_mutex);
    Entry *e = findEntry(jobId);
    if (!e)
        return;
    e->record.result = result;
    e->record.finishedAt = QDateTime::currentDateTime();
    e->record.progress.bytesDone = result.bytesWritten >= 0 ? result.bytesWritten : e->record.progress.bytesDone;
    if (result.cancelled)
        e->record.state = JobState::Cancelled;
    else if (result.ok)
        e->record.state = JobState::Completed;
    else
        e->record.state = JobState::Failed;
    if (!result.backendName.isEmpty())
        e->record.backendName = result.backendName;
    const JobRecord snapshot = e->record;
    if (e->task)
        e->task->deleteLater();
    e->task = nullptr;
    locker.unlock();
    emit jobUpdated(snapshot);
    tryStart();
}

void JobQueue::onLog(const QString &text)
{
    Logger::instance().info(text);
}

JobQueue::Entry *JobQueue::findEntry(const QString &jobId)
{
    for (Entry &e : m_entries) {
        if (e.record.id == jobId)
            return &e;
    }
    return nullptr;
}

const JobQueue::Entry *JobQueue::findEntry(const QString &jobId) const
{
    for (const Entry &e : m_entries) {
        if (e.record.id == jobId)
            return &e;
    }
    return nullptr;
}

} // namespace eis
