#include "backends/SevenZipBackend.h"

#include "core/Utils.h"
#include "logging/Logger.h"
#include "security/PathGuard.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <algorithm>
#include <QMap>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <csignal>
#include <sys/stat.h>
#include <unistd.h>

namespace eis {

namespace {

QString sevenZipPath()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(QStringLiteral("7za"));
    return path;
}

QStringList baseArgs(const QString &password)
{
    QStringList args;
    if (!password.isEmpty())
        args << (QStringLiteral("-p") + password);
    else
        args << QStringLiteral("-p"); // empty passphrase attempt; no prompt hang because stdin closed
    return args;
}

// Run 7z with given args and optional input; returns exit code and combined output.
struct RunResult {
    int exitCode = -1;
    QString output;
    bool timedOut = false;
};

RunResult run7z(const QStringList &args, int timeoutMs = 120000)
{
    RunResult result;
    const QString exe = sevenZipPath();
    if (exe.isEmpty()) {
        result.exitCode = 127;
        result.output = QStringLiteral("7-Zip executable not found");
        return result;
    }

    QProcess process;
    process.setProgram(exe);
    process.setArguments(args);
    process.start();
    if (!process.waitForStarted(5000)) {
        result.exitCode = 127;
        result.output = QStringLiteral("cannot start 7-Zip");
        return result;
    }
    process.closeWriteChannel();

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        result.timedOut = true;
        result.exitCode = process.exitCode();
        result.output = QString::fromLocal8Bit(process.readAllStandardOutput())
                      + QString::fromLocal8Bit(process.readAllStandardError());
        return result;
    }
    result.exitCode = process.exitCode();
    result.output = QString::fromLocal8Bit(process.readAllStandardOutput())
                  + QString::fromLocal8Bit(process.readAllStandardError());
    return result;
}

// Parse `7z l -slt -ba` output into entries.
struct ParsedEntry {
    QString path;
    qint64 size = -1;
    qint64 packed = -1;
    bool isDir = false;
    bool encrypted = false;
    QDateTime modified;
};

QVector<ParsedEntry> parseListing(const QString &output, int maxEntries, bool *truncated)
{
    QVector<ParsedEntry> entries;
    if (truncated)
        *truncated = false;

    // Blocks separated by blank lines.
    const QStringList blocks = output.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        QMap<QString, QString> fields;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq < 0)
                continue;
            const QString key = line.left(eq).trimmed();
            const QString value = line.mid(eq + 1).trimmed();
            if (!key.isEmpty())
                fields.insert(key, value);
        }
        if (!fields.contains(QStringLiteral("Path")))
            continue;

        if ((int)entries.size() >= maxEntries) {
            if (truncated)
                *truncated = true;
            break;
        }

        ParsedEntry e;
        e.path = fields.value(QStringLiteral("Path"));
        const QString sizeStr = fields.value(QStringLiteral("Size"));
        if (!sizeStr.isEmpty() && sizeStr != QLatin1String("-"))
            e.size = sizeStr.toLongLong();
        const QString packedStr = fields.value(QStringLiteral("Packed Size"));
        if (!packedStr.isEmpty() && packedStr != QLatin1String("-"))
            e.packed = packedStr.toLongLong();
        const QString attrs = fields.value(QStringLiteral("Attributes"));
        e.isDir = attrs.contains(QLatin1Char('D'));
        const QString enc = fields.value(QStringLiteral("Encrypted"));
        e.encrypted = (enc == QLatin1String("+") || enc.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0
                       || enc.compare(QStringLiteral("encrypted"), Qt::CaseInsensitive) == 0);
        const QString mod = fields.value(QStringLiteral("Modified"));
        if (!mod.isEmpty()) {
            // "2024-01-02 03:04:05" or "2024-01-02 03:04:05.123"
            QDateTime dt = QDateTime::fromString(mod.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (dt.isValid())
                e.modified = dt;
        }
        entries.append(e);
    }
    return entries;
}

bool isEncryptedError(const QString &output)
{
    const QString lowered = output.toLower();
    return lowered.contains(QStringLiteral("wrong password"))
        || lowered.contains(QStringLiteral("encrypted"))
        || lowered.contains(QStringLiteral("password"));
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

void sendSignal(qint64 pid, int sig)
{
    if (pid > 0)
        ::kill(static_cast<pid_t>(pid), sig);
}

} // namespace

bool SevenZipBackend::available() const
{
    return !sevenZipPath().isEmpty();
}

QStringList SevenZipBackend::formats() const
{
    // 7-Zip supports a broad set. Advertise the common ones; the registry
    // also parses `7z i` for the authoritative list when available.
    return {
        QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"),
        QStringLiteral("xz"), QStringLiteral("bz2"), QStringLiteral("zst"), QStringLiteral("lzma"),
        QStringLiteral("lz4"), QStringLiteral("iso"), QStringLiteral("cab"), QStringLiteral("rar"),
        QStringLiteral("ar"), QStringLiteral("cpio"), QStringLiteral("lha"), QStringLiteral("xar"),
        QStringLiteral("wim"), QStringLiteral("fat"), QStringLiteral("ntfs"), QStringLiteral("dmg")
    };
}

QString SevenZipBackend::version() const
{
    RunResult r = run7z({QStringLiteral("i")}, 15000);
    // First line contains version.
    const QString firstLine = r.output.section(QLatin1Char('\n'), 0, 0);
    return firstLine.trimmed();
}

QString SevenZipBackend::executablePath() const
{
    return sevenZipPath();
}

bool SevenZipBackend::probe(const QString &archivePath, const QString &password,
                            QString *formatOut, QString *errorOut)
{
    if (!available()) {
        if (errorOut)
            *errorOut = QStringLiteral("7-Zip is not installed");
        return false;
    }

    QStringList args = baseArgs(password);
    args << QStringLiteral("l") << QStringLiteral("-slt") << QStringLiteral("-ba") << QStringLiteral("--") << archivePath;
    RunResult r = run7z(args, 60000);
    if (r.exitCode == 0) {
        if (formatOut) {
            // Try to infer from first entry? 7-Zip doesn't print format in -ba mode.
            // Fall back to extension.
            *formatOut = QFileInfo(archivePath).suffix().toLower();
        }
        return true;
    }

    if (password.isEmpty() && isEncryptedError(r.output)) {
        if (formatOut)
            *formatOut = QFileInfo(archivePath).suffix().toLower();
        return true;
    }
    if (errorOut)
        *errorOut = r.output.trimmed().isEmpty() ? QStringLiteral("7-Zip probe failed") : r.output.trimmed();
    return false;
}

ArchiveListing SevenZipBackend::list(const QString &archivePath, const QString &password,
                                     int maxEntries, const std::atomic<bool> *cancel,
                                     const std::function<void(int)> &progress)
{
    ArchiveListing listing;
    listing.backendName = name();

    if (!available()) {
        listing.error = QStringLiteral("7-Zip is not installed");
        return listing;
    }

    QStringList args = baseArgs(password);
    args << QStringLiteral("l") << QStringLiteral("-slt") << QStringLiteral("-ba") << QStringLiteral("--") << archivePath;
    RunResult r = run7z(args, 300000);
    if (r.timedOut) {
        listing.error = QStringLiteral("7-Zip listing timed out");
        return listing;
    }

    listing.archiveSize = QFileInfo(archivePath).size();

    if (r.exitCode != 0 && !isEncryptedError(r.output)) {
        listing.error = r.output.trimmed().isEmpty() ? QStringLiteral("7-Zip listing failed") : r.output.trimmed();
        return listing;
    }

    bool truncated = false;
    QVector<ParsedEntry> parsed = parseListing(r.output, maxEntries, &truncated);
    int encryptedCount = 0;
    for (const ParsedEntry &pe : parsed) {
        if (cancel && cancel->load())
            break;
        QString error;
        const QString normalized = PathGuard::normalizeEntryPath(pe.path, &error);
        if (normalized.isEmpty())
            continue;

        ArchiveEntryInfo info;
        info.path = normalized;
        info.size = pe.size;
        info.compressedSize = pe.packed;
        info.isDir = pe.isDir;
        info.encrypted = pe.encrypted;
        info.modified = pe.modified;
        info.typeName = pe.isDir ? QStringLiteral("directory") : QStringLiteral("file");
        listing.entries.append(info);
        if (pe.encrypted)
            ++encryptedCount;
        if (progress && (listing.entries.size() % 250 == 0))
            progress(listing.entries.size());
    }

    listing.encryptedCount = encryptedCount;
    listing.totalEntries = listing.entries.size();
    listing.truncated = truncated;
    listing.partial = (cancel && cancel->load());
    listing.formatName = QFileInfo(archivePath).suffix().toLower();
    listing.ok = listing.error.isEmpty() && (r.exitCode == 0 || isEncryptedError(r.output));
    if (listing.partial && listing.error.isEmpty())
        listing.error = QStringLiteral("listing interrupted");
    return listing;
}

ExtractionResult SevenZipBackend::extract(const ExtractionOptions &options, JobControl &control,
                                          const ProgressCallback &progress)
{
    ExtractionResult result;
    result.backendName = name();

    if (!available()) {
        result.error = QStringLiteral("7-Zip is not installed");
        return result;
    }

    QString destError;
    const QString destRoot = PathGuard::canonicalDestination(options.destination, &destError);
    if (destRoot.isEmpty()) {
        result.error = destError;
        return result;
    }

    // Listing pass for totals + filter awareness.
    int totalFiles = -1;
    qint64 totalBytes = -1;
    ArchiveListing listing = this->list(options.archivePath, options.password, (int)options.maxEntries, nullptr, nullptr);
    if (listing.ok) {
        totalFiles = 0;
        totalBytes = 0;
        for (const ArchiveEntryInfo &e : listing.entries) {
            if (includedInSelection(e.path, options.includeEntries) == false)
                continue;
            if (e.isDir)
                continue;
            ++totalFiles;
            if (e.size >= 0)
                totalBytes += e.size;
        }
    }

    // Staging directory inside the destination's parent for same-filesystem moves.
    const QString stagingParent = QFileInfo(destRoot).absolutePath();
    QTemporaryDir staging(stagingParent + QStringLiteral("/.eis-staging-XXXXXX"));
    if (!staging.isValid()) {
        result.error = QStringLiteral("cannot create staging directory");
        return result;
    }
    const QString stagingPath = staging.path();

    // Build 7z args.
    QStringList args;
    args << QStringLiteral("x") << QStringLiteral("-y") << QStringLiteral("-bd");
    if (!options.password.isEmpty())
        args << (QStringLiteral("-p") + options.password);
    else
        args << QStringLiteral("-p");
    args << (QStringLiteral("-o") + stagingPath);
    args << QStringLiteral("--");
    args << options.archivePath;
    for (const QString &inc : options.includeEntries)
        args << inc;

    QProcess process;
    process.setProgram(sevenZipPath());
    process.setArguments(args);
    process.start();
    if (!process.waitForStarted(5000)) {
        result.error = QStringLiteral("cannot start 7-Zip");
        return result;
    }
    process.closeWriteChannel();

    QElapsedTimer timer;
    timer.start();
    bool cancelled = false;
    bool paused = false;

    while (process.state() != QProcess::NotRunning) {
        if (control.cancel.load()) {
            cancelled = true;
            process.terminate();
            break;
        }
        if (control.pause.load() && !paused) {
            sendSignal(process.processId(), SIGSTOP);
            paused = true;
        }
        if (!control.pause.load() && paused) {
            sendSignal(process.processId(), SIGCONT);
            paused = false;
        }

        // Poll staging for progress.
        qint64 bytesDone = 0;
        int filesDone = 0;
        QDirIterator it(stagingPath, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++filesDone;
            bytesDone += it.fileInfo().size();
        }
        ProgressInfo p;
        p.bytesDone = bytesDone;
        p.bytesTotal = (totalBytes > 0 ? totalBytes : -1);
        p.filesDone = filesDone;
        p.filesTotal = (totalFiles >= 0 ? totalFiles : -1);
        p.currentFile = QStringLiteral("extracting");
        p.status = QStringLiteral("extracting");
        p.elapsedMs = timer.elapsed();
        if (p.elapsedMs > 0 && bytesDone > 0)
            p.bytesPerSecond = 1000.0 * static_cast<double>(bytesDone) / static_cast<double>(p.elapsedMs);
        if (progress)
            progress(p);

        process.waitForFinished(300);
    }

    if (paused)
        sendSignal(process.processId(), SIGCONT);

    if (cancelled) {
        process.kill();
        process.waitForFinished(3000);
        result.cancelled = true;
        result.error = QStringLiteral("cancelled");
        return result;
    }

    if (process.exitCode() != 0) {
        const QString out = QString::fromLocal8Bit(process.readAllStandardOutput())
                          + QString::fromLocal8Bit(process.readAllStandardError());
        result.error = out.trimmed().isEmpty() ? QStringLiteral("7-Zip extraction failed") : out.trimmed();
        return result;
    }

    // Move staged files into destination with collision policy.
    int movedFiles = 0;
    int skipped = 0;
    int collisionsRenamed = 0;
    QStringList warnings;

    // Collect all files (and symlinks) in staging.
    struct StagedItem { QString stagingAbs; QString relative; bool isDir; bool isLink; };
    QVector<StagedItem> items;
    QDirIterator wit(stagingPath, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    // First create directories.
    // Gather dirs
    QVector<QString> dirs;
    QDirIterator dit(stagingPath, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dit.hasNext()) {
        dit.next();
        const QString rel = QDir(stagingPath).relativeFilePath(dit.filePath());
        dirs.append(rel);
    }
    // Sort by depth
    std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        return a.count(QLatin1Char('/')) < b.count(QLatin1Char('/'));
    });
    for (const QString &rel : dirs) {
        QString dirError;
        if (!PathGuard::ensureSafeDirectory(destRoot, rel, &dirError)) {
            warnings << dirError;
            continue;
        }
    }

    // Files and symlinks
    QDirIterator fit(stagingPath, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (fit.hasNext()) {
        fit.next();
        const QString rel = QDir(stagingPath).relativeFilePath(fit.filePath());
        QString normError;
        const QString normalized = PathGuard::normalizeEntryPath(rel, &normError);
        if (normalized.isEmpty()) {
            warnings << QStringLiteral("skipped unsafe entry: %1").arg(rel);
            continue;
        }
        if (!includedInSelection(normalized, options.includeEntries))
            continue;

        const QFileInfo fi(fit.filePath());
        bool isLink = fi.isSymLink();
        // Capture the real POSIX mode before the file is moved out of staging.
        struct stat stagedStat {};
        const bool haveStagedStat = !isLink
            && ::stat(QFile::encodeName(fit.filePath()).constData(), &stagedStat) == 0;
        const QString destPath = PathGuard::safeJoin(destRoot, normalized);

        if (isLink) {
            const QString target = fi.symLinkTarget();
            // fi.symLinkTarget returns resolved absolute; convert to relative? 7z symlink target may be relative.
            // QFileInfo::symLinkTarget returns absolute resolved path, not original. Use readlink instead.
            char buf[4096];
            const ssize_t n = ::readlink(fit.filePath().toUtf8().constData(), buf, sizeof(buf) - 1);
            QString linkTarget;
            if (n > 0) {
                buf[n] = '\0';
                linkTarget = QString::fromLocal8Bit(buf);
            }
            PathGuard::LinkCheck check = PathGuard::validateLinkTarget(normalized, linkTarget, false);
            if (!check.ok) {
                warnings << QStringLiteral("skipped unsafe symlink %1: %2").arg(normalized, check.error);
                continue;
            }
            bool skip = false;
            bool renamed = false;
            const QString finalPath = PathGuard::uniqueTargetPath(destPath, options.collision, &skip, &renamed);
            if (skip) {
                ++skipped;
                continue;
            }
            if (renamed)
                ++collisionsRenamed;
            ::unlink(QFile::encodeName(finalPath).constData());
            if (::symlink(linkTarget.toUtf8().constData(), QFile::encodeName(finalPath).constData()) != 0) {
                warnings << QStringLiteral("cannot create symlink %1").arg(finalPath);
                continue;
            }
            ++movedFiles;
            continue;
        }

        bool skip = false;
        bool renamed = false;
        const QString finalPath = PathGuard::uniqueTargetPath(destPath, options.collision, &skip, &renamed);
        if (skip) {
            ++skipped;
            continue;
        }
        if (renamed)
            ++collisionsRenamed;

        // Move file (rename). If cross-filesystem, copy+delete.
        if (::rename(fit.filePath().toUtf8().constData(), QFile::encodeName(finalPath).constData()) != 0) {
            // fallback copy
            QFile src(fit.filePath());
            if (src.copy(finalPath)) {
                src.remove();
            } else {
                warnings << QStringLiteral("cannot move %1: %2").arg(normalized, src.errorString());
                continue;
            }
        }
        if (options.preservePermissions && haveStagedStat) {
            ::chmod(QFile::encodeName(finalPath).constData(), stagedStat.st_mode & 07777);
        }
        ++movedFiles;
    }

    staging.setAutoRemove(true);

    result.filesExtracted = movedFiles;
    result.skipped = skipped;
    result.collisionsRenamed = collisionsRenamed;
    result.warnings = warnings;
    result.bytesWritten = -1;
    result.durationMs = timer.elapsed();
    if (result.error.isEmpty()) {
        result.ok = true;
    }

    ProgressInfo finalProgress;
    finalProgress.bytesDone = (totalBytes > 0 ? totalBytes : -1);
    finalProgress.bytesTotal = (totalBytes > 0 ? totalBytes : -1);
    finalProgress.filesDone = movedFiles;
    finalProgress.filesTotal = (totalFiles >= 0 ? totalFiles : -1);
    finalProgress.status = result.ok ? QStringLiteral("completed") : QStringLiteral("failed");
    finalProgress.elapsedMs = result.durationMs;
    if (progress)
        progress(finalProgress);

    Logger::instance().info(QStringLiteral("7z extract %1: %2 files")
                               .arg(options.archivePath).arg(movedFiles));
    return result;
}

} // namespace eis
