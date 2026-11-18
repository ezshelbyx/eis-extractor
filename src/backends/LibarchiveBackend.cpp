#include "backends/LibarchiveBackend.h"

#include "core/Utils.h"
#include "logging/Logger.h"
#include "security/PathGuard.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <archive.h>
#include <archive_entry.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cstring>

namespace eis {

namespace {

struct OpenResult {
    archive *handle = nullptr;
    QString error;
    bool ok = false;
};

OpenResult openArchive(const QString &path, const QString &password, QString *formatOut)
{
    OpenResult result;
    archive *a = archive_read_new();
    if (!a) {
        result.error = QStringLiteral("cannot allocate libarchive reader");
        return result;
    }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (!password.isEmpty())
        archive_read_add_passphrase(a, password.toUtf8().constData());

    const int rc = archive_read_open_filename(a, QFile::encodeName(path).constData(), 10240);
    if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
        const QString msg = QString::fromUtf8(archive_error_string(a));
        archive_read_close(a);
        archive_read_free(a);
        result.error = msg.isEmpty() ? QStringLiteral("cannot open archive") : msg;
        return result;
    }
    if (formatOut) {
        const char *fmt = archive_format_name(a);
        if (fmt && *fmt)
            *formatOut = QString::fromUtf8(fmt);
    }
    result.handle = a;
    result.ok = true;
    return result;
}

void closeArchive(archive *a)
{
    if (!a)
        return;
    archive_read_close(a);
    archive_read_free(a);
}

bool isEncryptedEntry(archive_entry *entry)
{
#if defined(ARCHIVE_VERSION_NUMBER) && ARCHIVE_VERSION_NUMBER >= 3004000
    return archive_entry_is_encrypted(entry) != 0;
#else
    Q_UNUSED(entry)
    return false;
#endif
}

bool includedInSelection(const QString &path, const QStringList &includeEntries)
{
    if (includeEntries.isEmpty())
        return true;
    for (const QString &inc : includeEntries) {
        if (path == inc || path.startsWith(inc + QLatin1Char('/')))
            return true;
    }
    return false;
}

QString entryTypeName(archive_entry *entry)
{
    const mode_t type = archive_entry_filetype(entry);
    if (S_ISDIR(type))
        return QStringLiteral("directory");
    if (S_ISLNK(type))
        return QStringLiteral("symlink");
    if (S_ISREG(type))
        return QStringLiteral("file");
    if (S_ISFIFO(type))
        return QStringLiteral("fifo");
    if (S_ISCHR(type) || S_ISBLK(type))
        return QStringLiteral("device");
    return QStringLiteral("other");
}

} // namespace

QStringList LibarchiveBackend::formats() const
{
    return {
        QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("tar"), QStringLiteral("gz"),
        QStringLiteral("xz"), QStringLiteral("bz2"), QStringLiteral("zst"), QStringLiteral("lz4"),
        QStringLiteral("lzma"), QStringLiteral("cpio"), QStringLiteral("iso"), QStringLiteral("ar"),
        QStringLiteral("cab"), QStringLiteral("lha"), QStringLiteral("xar"), QStringLiteral("rar"),
        QStringLiteral("warc"), QStringLiteral("mtree")
    };
}

QString LibarchiveBackend::version() const
{
    return QString::fromUtf8(archive_version_string());
}

bool LibarchiveBackend::probe(const QString &archivePath, const QString &password,
                              QString *formatOut, QString *errorOut)
{
    QString format;
    OpenResult opened = openArchive(archivePath, password, &format);
    if (!opened.ok) {
        // Distinguish encrypted archives: libarchive reports a passphrase error.
        const QString lowered = opened.error.toLower();
        if (password.isEmpty() && (lowered.contains(QStringLiteral("passphrase"))
                                  || lowered.contains(QStringLiteral("password"))
                                  || lowered.contains(QStringLiteral("encrypted")))) {
            if (formatOut)
                *formatOut = format.isEmpty() ? QFileInfo(archivePath).suffix().toLower() : format;
            return true;
        }
        if (errorOut)
            *errorOut = opened.error;
        return false;
    }

    // Try to read at least one header to confirm the format is real.
    archive_entry *entry = nullptr;
    const int rc = archive_read_next_header(opened.handle, &entry);
    if (rc == ARCHIVE_OK || rc == ARCHIVE_WARN || rc == ARCHIVE_EOF) {
        if (formatOut) {
            const char *fmt = archive_format_name(opened.handle);
            if (fmt && *fmt)
                *formatOut = QString::fromUtf8(fmt);
            else if (formatOut->isEmpty())
                *formatOut = format.isEmpty() ? QFileInfo(archivePath).suffix().toLower() : format;
        }
        closeArchive(opened.handle);
        return true;
    }

    const QString msg = QString::fromUtf8(archive_error_string(opened.handle));
    closeArchive(opened.handle);
    const QString lowered = msg.toLower();
    if (password.isEmpty() && (lowered.contains(QStringLiteral("passphrase"))
                              || lowered.contains(QStringLiteral("password"))
                              || lowered.contains(QStringLiteral("encrypted")))) {
        if (formatOut)
            *formatOut = format.isEmpty() ? QFileInfo(archivePath).suffix().toLower() : format;
        return true;
    }
    if (errorOut)
        *errorOut = msg.isEmpty() ? opened.error : msg;
    return false;
}

ArchiveListing LibarchiveBackend::list(const QString &archivePath, const QString &password,
                                       int maxEntries, const std::atomic<bool> *cancel,
                                       const std::function<void(int)> &progress)
{
    ArchiveListing listing;
    listing.backendName = name();

    QString format;
    OpenResult opened = openArchive(archivePath, password, &format);
    if (!opened.ok) {
        listing.error = opened.error;
        return listing;
    }

    listing.formatName = format;
    listing.filterName = QString::fromUtf8(archive_filter_name(opened.handle, 0));
    listing.archiveSize = QFileInfo(archivePath).size();

    archive_entry *entry = nullptr;
    int seen = 0;
    bool truncated = false;
    while (true) {
        if (cancel && cancel->load())
            break;
        const int rc = archive_read_next_header(opened.handle, &entry);
        if (rc == ARCHIVE_EOF)
            break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            const QString msg = QString::fromUtf8(archive_error_string(opened.handle));
            if (!msg.isEmpty())
                listing.error = msg;
            listing.partial = true;
            break;
        }

        if (seen >= maxEntries) {
            truncated = true;
            break;
        }

        const QString rawPath = QString::fromUtf8(archive_entry_pathname(entry));
        QString error;
        const QString normalized = PathGuard::normalizeEntryPath(rawPath, &error);
        if (normalized.isEmpty()) {
            (void)error;
            ++seen;
            continue;
        }

        ArchiveEntryInfo info;
        info.path = normalized;
        info.size = archive_entry_size(entry);
        info.compressedSize = -1;
        info.isDir = S_ISDIR(archive_entry_filetype(entry));
        info.isSymlink = S_ISLNK(archive_entry_filetype(entry));
        info.isHardlink = (archive_entry_hardlink(entry) != nullptr);
        info.encrypted = isEncryptedEntry(entry);
        if (info.isSymlink || info.isHardlink) {
            const char *target = info.isHardlink ? archive_entry_hardlink(entry)
                                                : archive_entry_symlink(entry);
            info.linkTarget = target ? QString::fromUtf8(target) : QString();
        }
        info.typeName = entryTypeName(entry);
        const time_t mtime = archive_entry_mtime(entry);
        if (mtime > 0)
            info.modified = QDateTime::fromSecsSinceEpoch(mtime);

        listing.entries.append(info);
        if (info.encrypted)
            ++listing.encryptedCount;
        ++seen;
        listing.totalEntries = seen;

        if (progress && (seen % 250 == 0))
            progress(seen);
    }

    closeArchive(opened.handle);

    if (cancel && cancel->load())
        listing.partial = true;
    if (truncated) {
        listing.truncated = true;
        listing.partial = true;
    }
    listing.ok = listing.error.isEmpty() || listing.entries.size() > 0;
    if (listing.error.isEmpty() && listing.entries.isEmpty() && listing.partial)
        listing.error = QStringLiteral("listing interrupted");
    return listing;
}

ExtractionResult LibarchiveBackend::extract(const ExtractionOptions &options, JobControl &control,
                                            const ProgressCallback &progress)
{
    ExtractionResult result;
    result.backendName = name();

    QString destError;
    const QString destRoot = PathGuard::canonicalDestination(options.destination, &destError);
    if (destRoot.isEmpty()) {
        result.error = destError;
        return result;
    }

    // ---- Pass 1: headers, totals, bomb guard, filter ----
    QString format;
    OpenResult opened = openArchive(options.archivePath, options.password, &format);
    if (!opened.ok) {
        result.error = opened.error;
        return result;
    }

    const qint64 archiveSize = QFileInfo(options.archivePath).size();

    qint64 totalBytes = 0;
    qint64 totalFiles = 0;
    int totalEntries = 0;
    QStringList warnings;

    archive_entry *entry = nullptr;
    while (true) {
        if (control.cancel.load()) {
            result.cancelled = true;
            break;
        }
        while (control.pause.load() && !control.cancel.load())
            QThread::msleep(100);
        if (control.cancel.load()) {
            result.cancelled = true;
            break;
        }

        const int rc = archive_read_next_header(opened.handle, &entry);
        if (rc == ARCHIVE_EOF)
            break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            const QString msg = QString::fromUtf8(archive_error_string(opened.handle));
            result.error = msg.isEmpty() ? QStringLiteral("cannot read archive header") : msg;
            closeArchive(opened.handle);
            return result;
        }

        const QString rawPath = QString::fromUtf8(archive_entry_pathname(entry));
        QString normError;
        const QString normalized = PathGuard::normalizeEntryPath(rawPath, &normError);
        if (normalized.isEmpty()) {
            warnings << QStringLiteral("skipped unsafe entry: %1 (%2)").arg(rawPath, normError);
            continue;
        }
        if (!includedInSelection(normalized, options.includeEntries))
            continue;

        if (isEncryptedEntry(entry) && options.password.isEmpty()) {
            result.error = QStringLiteral("archive contains encrypted entries; password required");
            closeArchive(opened.handle);
            return result;
        }

        ++totalEntries;
        if (totalEntries > options.maxEntries) {
            result.error = QStringLiteral("archive exceeds maximum entry count (%1)").arg(options.maxEntries);
            closeArchive(opened.handle);
            return result;
        }

        if (!S_ISDIR(archive_entry_filetype(entry))) {
            const qint64 size = archive_entry_size(entry);
            if (size >= 0)
                totalBytes += size;
            ++totalFiles;
        }

        if (control.cancel.load()) {
            result.cancelled = true;
            break;
        }
    }

    closeArchive(opened.handle);

    if (result.cancelled) {
        result.error = QStringLiteral("cancelled");
        return result;
    }
    if (!result.error.isEmpty())
        return result;

    // Bomb guard: only when the archive is large enough to matter.
    if (archiveSize > 0 && totalBytes > 1024LL * 1024 * 1024
        && totalBytes > static_cast<qint64>(options.maxCompressionRatio * static_cast<double>(archiveSize))) {
        result.error = QStringLiteral("possible archive bomb: uncompressed size %1 exceeds %2x the archive size")
                           .arg(totalBytes).arg(options.maxCompressionRatio, 0, 'f', 0);
        return result;
    }
    if (totalBytes > options.maxTotalBytes) {
        result.error = QStringLiteral("archive exceeds maximum total size (%1)").arg(options.maxTotalBytes);
        return result;
    }

    // ---- Pass 2: extract ----
    opened = openArchive(options.archivePath, options.password, &format);
    if (!opened.ok) {
        result.error = opened.error;
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    qint64 bytesDone = 0;
    qint64 filesDone = 0;
    int collisionsRenamed = 0;
    int skipped = 0;

    auto emitProgress = [&](const QString &currentFile, const QString &status) {
        ProgressInfo p;
        p.bytesDone = bytesDone;
        p.bytesTotal = (totalBytes > 0 ? totalBytes : -1);
        p.filesDone = filesDone;
        p.filesTotal = totalFiles;
        p.currentFile = currentFile;
        p.status = status;
        p.elapsedMs = timer.elapsed();
        if (p.elapsedMs > 0)
            p.bytesPerSecond = 1000.0 * static_cast<double>(bytesDone) / static_cast<double>(p.elapsedMs);
        if (progress)
            progress(p);
    };

    emitProgress(QString(), QStringLiteral("starting"));

    while (true) {
        if (control.cancel.load()) {
            result.cancelled = true;
            break;
        }
        while (control.pause.load() && !control.cancel.load())
            QThread::msleep(100);
        if (control.cancel.load()) {
            result.cancelled = true;
            break;
        }

        const int rc = archive_read_next_header(opened.handle, &entry);
        if (rc == ARCHIVE_EOF)
            break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            const QString msg = QString::fromUtf8(archive_error_string(opened.handle));
            result.error = msg.isEmpty() ? QStringLiteral("cannot read archive header") : msg;
            break;
        }

        const QString rawPath = QString::fromUtf8(archive_entry_pathname(entry));
        QString normError;
        const QString normalized = PathGuard::normalizeEntryPath(rawPath, &normError);
        if (normalized.isEmpty()) {
            warnings << QStringLiteral("skipped unsafe entry: %1 (%2)").arg(rawPath, normError);
            archive_read_data_skip(opened.handle);
            continue;
        }
        if (!includedInSelection(normalized, options.includeEntries)) {
            archive_read_data_skip(opened.handle);
            continue;
        }

        emitProgress(normalized, QStringLiteral("extracting"));

        const bool isDir = S_ISDIR(archive_entry_filetype(entry));
        const bool isLink = S_ISLNK(archive_entry_filetype(entry));
        const bool isHard = (archive_entry_hardlink(entry) != nullptr);

        // Directory entry
        if (isDir) {
            QString dirError;
            if (!PathGuard::ensureSafeDirectory(destRoot, normalized, &dirError)) {
                result.error = dirError;
                break;
            }
            if (options.preservePermissions) {
                const QString full = PathGuard::safeJoin(destRoot, normalized);
                ::chmod(QFile::encodeName(full).constData(), archive_entry_perm(entry) & 0777);
            }
            continue;
        }

        // Symlink entry
        if (isLink) {
            const char *target = archive_entry_symlink(entry);
            const QString targetStr = target ? QString::fromUtf8(target) : QString();
            PathGuard::LinkCheck check = PathGuard::validateLinkTarget(normalized, targetStr, false);
            if (!check.ok) {
                warnings << QStringLiteral("skipped unsafe symlink %1: %2").arg(normalized, check.error);
                archive_read_data_skip(opened.handle);
                continue;
            }
            const QString parentRel = normalized.contains(QLatin1Char('/'))
                                          ? normalized.left(normalized.lastIndexOf(QLatin1Char('/')))
                                          : QString();
            QString dirError;
            if (!parentRel.isEmpty() && !PathGuard::ensureSafeDirectory(destRoot, parentRel, &dirError)) {
                result.error = dirError;
                break;
            }

            const QString destPath = PathGuard::safeJoin(destRoot, normalized);
            bool skip = false;
            bool renamed = false;
            const QString finalPath = PathGuard::uniqueTargetPath(destPath, options.collision, &skip, &renamed);
            if (skip) {
                ++skipped;
                archive_read_data_skip(opened.handle);
                continue;
            }
            if (renamed)
                ++collisionsRenamed;

            ::unlink(QFile::encodeName(finalPath).constData());
            if (::symlink(targetStr.toUtf8().constData(), QFile::encodeName(finalPath).constData()) != 0) {
                result.error = QStringLiteral("cannot create symlink %1: %2")
                                   .arg(finalPath, QString::fromLocal8Bit(std::strerror(errno)));
                break;
            }
            continue;
        }

        // Hardlink entry
        if (isHard) {
            const char *target = archive_entry_hardlink(entry);
            const QString targetStr = target ? QString::fromUtf8(target) : QString();
            PathGuard::LinkCheck check = PathGuard::validateLinkTarget(normalized, targetStr, true);
            if (!check.ok) {
                warnings << QStringLiteral("skipped unsafe hardlink %1: %2").arg(normalized, check.error);
                archive_read_data_skip(opened.handle);
                continue;
            }
            const QString parentRel = normalized.contains(QLatin1Char('/'))
                                          ? normalized.left(normalized.lastIndexOf(QLatin1Char('/')))
                                          : QString();
            QString dirError;
            if (!parentRel.isEmpty() && !PathGuard::ensureSafeDirectory(destRoot, parentRel, &dirError)) {
                result.error = dirError;
                break;
            }
            const QString sourceAbs = PathGuard::safeJoin(destRoot, check.resolved);
            const QString destPath = PathGuard::safeJoin(destRoot, normalized);
            bool skip = false;
            bool renamed = false;
            const QString finalPath = PathGuard::uniqueTargetPath(destPath, options.collision, &skip, &renamed);
            if (skip) {
                ++skipped;
                archive_read_data_skip(opened.handle);
                continue;
            }
            if (renamed)
                ++collisionsRenamed;

            if (!PathGuard::pathExistsNoFollow(sourceAbs)) {
                warnings << QStringLiteral("hardlink target not present, skipped: %1").arg(normalized);
                ::unlink(QFile::encodeName(finalPath).constData());
                ++skipped;
                archive_read_data_skip(opened.handle);
                continue;
            }
            ::unlink(QFile::encodeName(finalPath).constData());
            if (::link(QFile::encodeName(sourceAbs).constData(), QFile::encodeName(finalPath).constData()) != 0) {
                warnings << QStringLiteral("could not hardlink %1: %2").arg(normalized, QString::fromLocal8Bit(std::strerror(errno)));
                // Fall back to copying is complex; skip instead.
                ::unlink(QFile::encodeName(finalPath).constData());
                ++skipped;
            } else {
                ++filesDone;
                result.filesExtracted++;
            }
            archive_read_data_skip(opened.handle);
            continue;
        }

        // Regular file
        const QString parentRel = normalized.contains(QLatin1Char('/'))
                                      ? normalized.left(normalized.lastIndexOf(QLatin1Char('/')))
                                      : QString();
        QString dirError;
        if (!parentRel.isEmpty() && !PathGuard::ensureSafeDirectory(destRoot, parentRel, &dirError)) {
            result.error = dirError;
            break;
        }

        const QString destPath = PathGuard::safeJoin(destRoot, normalized);
        bool skip = false;
        bool renamed = false;
        const QString finalPath = PathGuard::uniqueTargetPath(destPath, options.collision, &skip, &renamed);
        if (skip) {
            ++skipped;
            archive_read_data_skip(opened.handle);
            continue;
        }
        if (renamed)
            ++collisionsRenamed;

        const int mode = options.preservePermissions ? (archive_entry_perm(entry) & 0777) : 0644;
        // Strip setuid/setgid explicitly.
        const int safeMode = mode & ~06000;

        int flags = O_WRONLY | O_CREAT | O_NOFOLLOW;
        if (options.collision == CollisionPolicy::Overwrite && finalPath == destPath)
            flags |= O_TRUNC;
        else
            flags |= O_EXCL;

        const int fd = ::open(QFile::encodeName(finalPath).constData(), flags, safeMode);
        if (fd < 0) {
            result.error = QStringLiteral("cannot create file %1: %2")
                               .arg(finalPath, QString::fromLocal8Bit(std::strerror(errno)));
            break;
        }

        // Stream data blocks.
        bool writeFailed = false;
        const void *buff = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (true) {
            if (control.cancel.load()) {
                writeFailed = true;
                break;
            }
            while (control.pause.load() && !control.cancel.load())
                QThread::msleep(100);
            if (control.cancel.load()) {
                writeFailed = true;
                break;
            }

            const int r = archive_read_data_block(opened.handle, &buff, &size, &offset);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
                const QString msg = QString::fromUtf8(archive_error_string(opened.handle));
                result.error = msg.isEmpty() ? QStringLiteral("read error") : msg;
                writeFailed = true;
                break;
            }
            if (size == 0)
                continue;

            // Write at offset (sparse handling): seek if needed.
            // Simple: write sequentially; libarchive usually sequential.
            ssize_t written = ::write(fd, buff, size);
            if (written < 0 || static_cast<size_t>(written) != size) {
                result.error = QStringLiteral("write error on %1: %2")
                                   .arg(finalPath, QString::fromLocal8Bit(std::strerror(errno)));
                writeFailed = true;
                break;
            }
            bytesDone += static_cast<qint64>(size);
            emitProgress(normalized, QStringLiteral("extracting"));
        }

        ::close(fd);

        if (control.cancel.load()) {
            result.cancelled = true;
            ::unlink(QFile::encodeName(finalPath).constData());
            break;
        }

        if (writeFailed) {
            ::unlink(QFile::encodeName(finalPath).constData());
            if (!result.error.isEmpty())
                break;
            // cancellation handled above
            break;
        }

        if (options.preservePermissions) {
            ::chmod(QFile::encodeName(finalPath).constData(), safeMode);
            const time_t mtime = archive_entry_mtime(entry);
            if (mtime > 0) {
                struct utimbuf times;
                times.actime = mtime;
                times.modtime = mtime;
                ::utime(QFile::encodeName(finalPath).constData(), &times);
            }
        }

        ++filesDone;
        ++result.filesExtracted;
        result.bytesWritten += archive_entry_size(entry);
    }

    closeArchive(opened.handle);

    if (result.cancelled) {
        result.error = QStringLiteral("cancelled");
    } else if (result.error.isEmpty()) {
        result.ok = true;
    }

    result.durationMs = timer.elapsed();
    result.skipped = skipped;
    result.collisionsRenamed = collisionsRenamed;
    result.warnings = warnings;

    ProgressInfo finalProgress;
    finalProgress.bytesDone = bytesDone;
    finalProgress.bytesTotal = totalBytes > 0 ? totalBytes : -1;
    finalProgress.filesDone = filesDone;
    finalProgress.filesTotal = totalFiles;
    finalProgress.currentFile = result.cancelled ? QStringLiteral("cancelled") : QString();
    finalProgress.status = result.ok ? QStringLiteral("completed") : (result.cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"));
    finalProgress.elapsedMs = result.durationMs;
    if (result.durationMs > 0)
        finalProgress.bytesPerSecond = 1000.0 * static_cast<double>(bytesDone) / static_cast<double>(result.durationMs);
    if (progress)
        progress(finalProgress);

    Logger::instance().info(QStringLiteral("libarchive extract %1: %2 files, %3")
                               .arg(options.archivePath)
                               .arg(result.filesExtracted)
                               .arg(result.ok ? QStringLiteral("ok") : result.error));

    return result;
}

} // namespace eis
