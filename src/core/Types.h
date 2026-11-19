#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

namespace eis {

enum class JobState { Queued, Running, Paused, Completed, Failed, Cancelled };
enum class BackendKind { None, Libarchive, SevenZip };
enum class CollisionPolicy { Rename, Skip, Overwrite };

QString jobStateLabel(JobState state);

// Shared, thread-safe control block for one extraction job. The UI thread
// flips the flags; the worker thread polls them.
struct JobControl {
    std::atomic<bool> cancel{false};
    std::atomic<bool> pause{false};
};

struct ExtractionOptions {
    QString archivePath;
    QString destination;
    QString password;
    QStringList includeEntries; // empty = every entry; otherwise exact paths or subtrees
    CollisionPolicy collision = CollisionPolicy::Rename;
    bool testOnly = false;
    bool preservePermissions = true;
    qint64 maxEntries = 1000000;
    qint64 maxTotalBytes = 200LL * 1024 * 1024 * 1024; // 200 GiB
    double maxCompressionRatio = 1000.0;               // bomb guard, only above 1 GiB
};

struct ProgressInfo {
    qint64 bytesDone = 0;
    qint64 bytesTotal = -1; // -1 = unknown, never faked
    qint64 filesDone = 0;
    qint64 filesTotal = -1;
    qint64 entriesSeen = 0;
    QString currentFile;
    QString status;
    double bytesPerSecond = 0.0;
    qint64 elapsedMs = 0;
    qint64 outputBytes = -1;              // compression output so far, -1 = unknown
    double compressionRatio = -1.0;       // input/output, -1 = unknown
    bool indeterminate() const { return bytesTotal <= 0; }
};

struct ExtractionResult {
    bool ok = false;
    bool cancelled = false;
    bool testOnly = false;
    QString error;
    QStringList warnings;
    qint64 filesExtracted = 0;
    qint64 bytesWritten = 0;
    qint64 durationMs = 0;
    int skipped = 0;
    int collisionsRenamed = 0;
    QString backendName;
};

struct JobRecord {
    QString id;
    ExtractionOptions options;
    JobState state = JobState::Queued;
    ProgressInfo progress;
    ExtractionResult result;
    QString backendName;
    QDateTime queuedAt;
    QDateTime startedAt;
    QDateTime finishedAt;
    int attempt = 1;
};

struct ArchiveEntryInfo {
    QString path;
    qint64 size = -1;
    qint64 compressedSize = -1;
    bool isDir = false;
    bool isSymlink = false;
    bool isHardlink = false;
    bool encrypted = false;
    QString linkTarget;
    QString typeName;
    QDateTime modified;
};

struct ArchiveListing {
    bool ok = false;
    bool partial = false;
    bool truncated = false;
    QString error;
    QString formatName;
    QString filterName;
    QString backendName;
    qint64 archiveSize = 0;
    int totalEntries = 0;
    int encryptedCount = 0;
    QVector<ArchiveEntryInfo> entries;
};

struct IntegrityResult {
    bool completed = false;
    bool ok = false;
    bool cancelled = false;
    bool testOnly = false;
    QString error;
    QString backendName;
    QString formatName;
    QString sha256;
    int expectedMatch = -1; // -1 not requested, 0 mismatch, 1 match
    qint64 bytesRead = 0;
    qint64 durationMs = 0;
    QStringList warnings;
};

enum class LogLevel { Debug, Info, Warning, Error, Success };

struct LogEntry {
    QDateTime time;
    LogLevel level = LogLevel::Info;
    QString text;
};

} // namespace eis

Q_DECLARE_METATYPE(eis::ProgressInfo)
Q_DECLARE_METATYPE(eis::ExtractionResult)
Q_DECLARE_METATYPE(eis::JobRecord)
Q_DECLARE_METATYPE(eis::ArchiveEntryInfo)
Q_DECLARE_METATYPE(eis::ArchiveListing)
Q_DECLARE_METATYPE(eis::IntegrityResult)
Q_DECLARE_METATYPE(eis::LogEntry)
