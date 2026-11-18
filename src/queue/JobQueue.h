#pragma once

#include "core/Types.h"
#include "extraction/ExtractionTask.h"

#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QVector>
#include <memory>

namespace eis {

class JobQueue : public QObject {
    Q_OBJECT
public:
    static JobQueue &instance();

    QString enqueue(const ExtractionOptions &options);
    void cancel(const QString &jobId);
    void pause(const QString &jobId);
    void resume(const QString &jobId);
    void retry(const QString &jobId);

    JobRecord job(const QString &jobId) const;
    QVector<JobRecord> jobs() const;

    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int value);

signals:
    void jobAdded(const eis::JobRecord &record);
    void jobUpdated(const eis::JobRecord &record);
    void jobRemoved(const QString &jobId);
    void progressUpdated(const QString &jobId, const eis::ProgressInfo &progress);

private slots:
    void onStarted(const QString &jobId);
    void onProgress(const QString &jobId, const ProgressInfo &progress);
    void onFinished(const QString &jobId, const ExtractionResult &result);
    void onLog(const QString &text);

private:
    explicit JobQueue(QObject *parent = nullptr);

    struct Entry {
        JobRecord record;
        std::shared_ptr<JobControl> control;
        ExtractionTask *task = nullptr;
    };

    void tryStart();
    Entry *findEntry(const QString &jobId);
    const Entry *findEntry(const QString &jobId) const;

    mutable QMutex m_mutex;
    QVector<Entry> m_entries;
    int m_maxConcurrent = 2;
    QThreadPool m_pool;
};

} // namespace eis
