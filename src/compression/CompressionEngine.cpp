#include "compression/CompressionEngine.h"

#include "core/Capabilities.h"
#include "core/Utils.h"
#include "logging/Logger.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QThread>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace eis {

namespace {

struct ScanItem {
    QString absolute;
    QString relative;
    qint64 size = 0;
    bool isDir = false;
    bool isSymlink = false;
    QString linkTarget;
    mode_t mode = 0644;
    qint64 mtime = 0;
};

struct ScanResult {
    QVector<ScanItem> items;
    qint64 totalBytes = 0;
    int totalFiles = 0;
    QStringList warnings;
    bool ok = false;
    QString error;
};

QString formatLibName(const QString &format)
{
    if (format == QLatin1String("zip"))
        return QStringLiteral("zip");
    if (format == QLatin1String("cpio"))
        return QStringLiteral("cpio");
    if (format == QLatin1String("tar") || format.startsWith(QLatin1String("tar.")))
        return QStringLiteral("pax");
    return QStringLiteral("raw");
}

QString filterLibName(const QString &format)
{
    if (format.endsWith(QLatin1String(".gz")) || format == QLatin1String("gz"))
        return QStringLiteral("gzip");
    if (format.endsWith(QLatin1String(".bz2")) || format == QLatin1String("bz2"))
        return QStringLiteral("bzip2");
    if (format.endsWith(QLatin1String(".xz")) || format == QLatin1String("xz"))
        return QStringLiteral("xz");
    if (format.endsWith(QLatin1String(".zst")) || format == QLatin1String("zst"))
        return QStringLiteral("zstd");
    if (format.endsWith(QLatin1String(".lz4")) || format == QLatin1String("lz4"))
        return QStringLiteral("lz4");
    if (format.endsWith(QLatin1String(".lzma")) || format == QLatin1String("lzma"))
        return QStringLiteral("lzma");
    return QString();
}

bool isStreamFormat(const QString &format)
{
    return format == QLatin1String("gz") || format == QLatin1String("bz2")
        || format == QLatin1String("xz") || format == QLatin1String("zst")
        || format == QLatin1String("lz4") || format == QLatin1String("lzma");
}

bool usesSevenZip(const QString &format, const QString &password, qint64 volumeSize)
{
    if (format == QLatin1String("7z"))
        return true;
    if (format == QLatin1String("zip") && (!password.isEmpty() || volumeSize > 0))
        return true;
    return false;
}

int sevenZipLevel(CompressionLevel level)
{
    switch (level) {
    case CompressionLevel::Store:
        return 0;
    case CompressionLevel::Fast:
        return 1;
    case CompressionLevel::Normal:
        return 5;
    case CompressionLevel::Maximum:
        return 9;
    }
    return 5;
}

int libarchiveLevel(CompressionLevel level)
{
    switch (level) {
    case CompressionLevel::Store:
        return 0;
    case CompressionLevel::Fast:
        return 1;
    case CompressionLevel::Normal:
        return 6;
    case CompressionLevel::Maximum:
        return 9;
    }
    return 6;
}

bool walkDirectory(const QString &absolute, const QString &relative, ScanResult *scan,
                   QSet<QString> *seen, int depth)
{
    if (depth > 128) {
        scan->warnings << QStringLiteral("directory nesting too deep, skipped: %1").arg(relative);
        return true;
    }
    QDir dir(absolute);
    if (!dir.exists()) {
        scan->warnings << QStringLiteral("cannot read directory: %1").arg(relative);
        return true;
    }
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const QFileInfo &info : entries) {
        const QString abs = info.absoluteFilePath();
        const QString rel = relative.isEmpty() ? info.fileName()
                                               : relative + QLatin1Char('/') + info.fileName();
        if (seen->contains(rel)) {
            scan->warnings << QStringLiteral("duplicate archive entry skipped: %1").arg(rel);
            continue;
        }

        struct stat st {};
        if (::lstat(QFile::encodeName(abs).constData(), &st) != 0) {
            scan->warnings << QStringLiteral("cannot stat: %1").arg(rel);
            continue;
        }

        if (S_ISLNK(st.st_mode)) {
            char buffer[4096];
            const ssize_t n = ::readlink(QFile::encodeName(abs).constData(), buffer, sizeof(buffer) - 1);
            ScanItem item;
            item.absolute = abs;
            item.relative = rel;
            item.isSymlink = true;
            item.mode = st.st_mode;
            item.mtime = st.st_mtime;
            if (n > 0) {
                buffer[n] = '\0';
                item.linkTarget = QString::fromLocal8Bit(buffer);
            }
            scan->items.append(item);
            scan->totalFiles++;
            seen->insert(rel);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            ScanItem item;
            item.absolute = abs;
            item.relative = rel;
            item.isDir = true;
            item.mode = st.st_mode;
            item.mtime = st.st_mtime;
            scan->items.append(item);
            seen->insert(rel);
            if (!walkDirectory(abs, rel, scan, seen, depth + 1))
                return false;
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            scan->warnings << QStringLiteral("special file skipped: %1").arg(rel);
            continue;
        }

        ScanItem item;
        item.absolute = abs;
        item.relative = rel;
        item.size = st.st_size;
        item.mode = st.st_mode;
        item.mtime = st.st_mtime;
        scan->items.append(item);
        scan->totalBytes += item.size;
        scan->totalFiles++;
        seen->insert(rel);
    }
    return true;
}

ScanResult scanSources(const QStringList &sources)
{
    ScanResult scan;
    QSet<QString> seen;

    for (const QString &source : sources) {
        const QFileInfo info(source);
        const QString abs = info.absoluteFilePath();
        struct stat st {};
        if (::lstat(QFile::encodeName(abs).constData(), &st) != 0) {
            scan.error = QStringLiteral("source does not exist: %1").arg(source);
            return scan;
        }
        const QString base = info.fileName();
        if (base.isEmpty() || base == QLatin1String(".") || base == QLatin1String("..")) {
            scan.error = QStringLiteral("invalid source name: %1").arg(source);
            return scan;
        }

        if (S_ISLNK(st.st_mode)) {
            char buffer[4096];
            const ssize_t n = ::readlink(QFile::encodeName(abs).constData(), buffer, sizeof(buffer) - 1);
            ScanItem item;
            item.absolute = abs;
            item.relative = base;
            item.isSymlink = true;
            item.mode = st.st_mode;
            item.mtime = st.st_mtime;
            if (n > 0) {
                buffer[n] = '\0';
                item.linkTarget = QString::fromLocal8Bit(buffer);
            }
            if (seen.contains(base)) {
                scan.warnings << QStringLiteral("duplicate archive entry skipped: %1").arg(base);
                continue;
            }
            scan.items.append(item);
            scan.totalFiles++;
            seen.insert(base);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (seen.contains(base)) {
                scan.warnings << QStringLiteral("duplicate archive entry skipped: %1").arg(base);
                continue;
            }
            ScanItem item;
            item.absolute = abs;
            item.relative = base;
            item.isDir = true;
            item.mode = st.st_mode;
            item.mtime = st.st_mtime;
            scan.items.append(item);
            seen.insert(base);
            if (!walkDirectory(abs, base, &scan, &seen, 0))
                return scan;
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            scan.warnings << QStringLiteral("special file skipped: %1").arg(source);
            continue;
        }

        if (seen.contains(base)) {
            scan.warnings << QStringLiteral("duplicate archive entry skipped: %1").arg(base);
            continue;
        }
        ScanItem item;
        item.absolute = abs;
        item.relative = base;
        item.size = st.st_size;
        item.mode = st.st_mode;
        item.mtime = st.st_mtime;
        scan.items.append(item);
        scan.totalBytes += item.size;
        scan.totalFiles++;
        seen.insert(base);
    }

    std::sort(scan.items.begin(), scan.items.end(), [](const ScanItem &a, const ScanItem &b) {
        return a.relative < b.relative;
    });
    scan.ok = true;
    return scan;
}

QString commonParent(const QStringList &sources)
{
    QStringList parents;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        parents << (info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    }
    if (parents.isEmpty())
        return QString();

    QString parent = parents.first();
    for (int i = 1; i < parents.size(); ++i) {
        while (!parents.at(i).startsWith(parent + QLatin1Char('/'))
               && parents.at(i) != parent) {
            const int slash = parent.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0) {
                parent = QStringLiteral("/");
                break;
            }
            parent = parent.left(slash);
        }
    }
    return parent;
}

QStringList relativeSources(const QStringList &sources, const QString &parent, QString *error)
{
    QStringList result;
    const QString cleanParent = QDir::cleanPath(parent);
    for (const QString &source : sources) {
        const QString abs = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
        if (abs == cleanParent) {
            // Compressing the common parent itself: use its base name.
            result << QFileInfo(abs).fileName();
            continue;
        }
        if (!abs.startsWith(cleanParent + QLatin1Char('/'))) {
            if (error)
                *error = QStringLiteral("source is outside the common parent: %1").arg(source);
            return {};
        }
        const QString rel = abs.mid(cleanParent.size() + 1);
        if (rel.startsWith(QLatin1String("../")) || rel == QLatin1String("..")) {
            if (error)
                *error = QStringLiteral("unsafe relative path: %1").arg(source);
            return {};
        }
        result << rel;
    }
    return result;
}

void cleanupVolumeSet(const QString &basePath)
{
    QFile::remove(basePath);
    for (int i = 1; i < 1000; ++i) {
        QFile::remove(basePath + QStringLiteral(".%1").arg(i, 3, 10, QLatin1Char('0')));
    }
}

QStringList volumeSuffixesFor(const QString &basePath)
{
    QStringList result;
    const QFileInfo info(basePath);
    const QDir dir = info.absoluteDir();
    const QString prefix = info.fileName() + QLatin1Char('.');
    for (const QFileInfo &entry : dir.entryInfoList(QDir::Files | QDir::Hidden, QDir::Name)) {
        if (!entry.fileName().startsWith(prefix))
            continue;
        const QString suffix = entry.fileName().mid(prefix.size());
        bool numeric = false;
        suffix.toInt(&numeric);
        if (numeric)
            result << entry.fileName();
    }
    return result;
}

qint64 outputSetSize(const QString &basePath)
{
    qint64 total = 0;
    const QStringList volumes = volumeSuffixesFor(basePath);
    if (!volumes.isEmpty()) {
        const QDir dir = QFileInfo(basePath).absoluteDir();
        for (const QString &name : volumes)
            total += QFileInfo(dir.filePath(name)).size();
    } else {
        total = QFileInfo(basePath).size();
    }
    return total;
}

bool moveOutputSet(const QString &tempBase, const QString &finalBase, bool overwrite,
                   QString *error)
{
    const QStringList volumes = volumeSuffixesFor(tempBase);
    if (!volumes.isEmpty()) {
        const QDir tempDir = QFileInfo(tempBase).absoluteDir();
        const QDir finalDir = QFileInfo(finalBase).absoluteDir();
        for (const QString &name : volumes) {
            const QString suffix = name.mid(QFileInfo(tempBase).fileName().size());
            const QString target = finalBase + suffix;
            if (QFileInfo::exists(target)) {
                if (!overwrite) {
                    if (error)
                        *error = QStringLiteral("output already exists: %1").arg(target);
                    return false;
                }
                QFile::remove(target);
            }
            if (!QFile::rename(tempDir.filePath(name), target)) {
                if (error)
                    *error = QStringLiteral("cannot rename output volume: %1").arg(target);
                return false;
            }
        }
        return true;
    }

    if (QFileInfo::exists(finalBase)) {
        if (!overwrite) {
            if (error)
                *error = QStringLiteral("output already exists: %1").arg(finalBase);
            return false;
        }
        QFile::remove(finalBase);
    }
    if (!QFile::rename(tempBase, finalBase)) {
        if (error)
            *error = QStringLiteral("cannot move archive into place: %1").arg(finalBase);
        return false;
    }
    return true;
}

QString makeTempPath(const QString &finalBase, QString *error)
{
    const QFileInfo info(finalBase);
    QDir dir = info.absoluteDir();
    if (!dir.exists()) {
        if (error)
            *error = QStringLiteral("output directory does not exist: %1").arg(dir.absolutePath());
        return QString();
    }
    QTemporaryFile temp(dir.filePath(QStringLiteral(".eis-part-XXXXXX")));
    temp.setAutoRemove(false);
    if (!temp.open()) {
        if (error)
            *error = QStringLiteral("cannot create temporary output in %1").arg(dir.absolutePath());
        return QString();
    }
    const QString path = temp.fileName();
    temp.close();
    temp.remove();
    return path;
}

bool writeLibarchive(const CompressionRequest &request, const ScanResult &scan,
                     const QString &tempPath, JobControl *control,
                     const CompressionEngine::ProgressCallback &progress,
                     CompressionResult *result)
{
    const QString format = formatLibName(request.format);
    const QString filter = filterLibName(request.format);

    archive *a = archive_write_new();
    if (!a) {
        result->error = QStringLiteral("cannot allocate archive writer");
        return false;
    }
    if (archive_write_set_format_by_name(a, format.toLatin1().constData()) != ARCHIVE_OK) {
        result->error = QStringLiteral("backend cannot write format: %1").arg(request.format);
        archive_write_free(a);
        return false;
    }
    if (!filter.isEmpty()
        && archive_write_add_filter_by_name(a, filter.toLatin1().constData()) != ARCHIVE_OK) {
        result->error = QStringLiteral("compression filter unavailable: %1").arg(filter);
        archive_write_free(a);
        return false;
    }

    const QString levelOption = QStringLiteral("compression-level=%1").arg(libarchiveLevel(request.level));
    archive_write_set_options(a, levelOption.toUtf8().constData());
    if (archive_write_open_filename(a, QFile::encodeName(tempPath).constData()) != ARCHIVE_OK) {
        result->error = QStringLiteral("cannot open output: %1")
                            .arg(QString::fromUtf8(archive_error_string(a)));
        archive_write_free(a);
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    qint64 bytesDone = 0;
    int filesDone = 0;
    QElapsedTimer lastEmit;
    lastEmit.start();

    auto reportProgress = [&](const QString &current, const QString &status) {
        if (!progress)
            return;
        ProgressInfo p;
        p.bytesDone = bytesDone;
        p.bytesTotal = scan.totalBytes > 0 ? scan.totalBytes : -1;
        p.filesDone = filesDone;
        p.filesTotal = scan.totalFiles;
        p.currentFile = current;
        p.status = status;
        p.elapsedMs = timer.elapsed();
        if (p.elapsedMs > 0 && bytesDone > 0)
            p.bytesPerSecond = 1000.0 * static_cast<double>(bytesDone) / p.elapsedMs;
        const qint64 outputNow = QFileInfo(tempPath).size();
        if (outputNow > 0 && bytesDone > 0) {
            p.outputBytes = outputNow;
            p.compressionRatio = static_cast<double>(bytesDone) / static_cast<double>(outputNow);
        }
        progress(p);
    };

    bool failed = false;
    for (const ScanItem &item : scan.items) {
        if (control && control->cancel.load()) {
            result->cancelled = true;
            break;
        }
        while (control && control->pause.load() && !control->cancel.load())
            QThread::msleep(100);
        if (control && control->cancel.load()) {
            result->cancelled = true;
            break;
        }

        reportProgress(item.relative, QStringLiteral("compressing"));

        archive_entry *entry = archive_entry_new();
        if (!entry) {
            result->error = QStringLiteral("cannot allocate archive entry");
            failed = true;
            break;
        }
        archive_entry_set_pathname(entry, item.relative.toUtf8().constData());
        archive_entry_set_perm(entry, item.mode & 07777);
        archive_entry_set_mtime(entry, static_cast<time_t>(item.mtime), 0);
        archive_entry_set_uid(entry, 0);
        archive_entry_set_gid(entry, 0);
        archive_entry_set_uname(entry, nullptr);
        archive_entry_set_gname(entry, nullptr);

        if (item.isSymlink) {
            archive_entry_set_filetype(entry, AE_IFLNK);
            archive_entry_set_perm(entry, 0777);
            archive_entry_set_symlink(entry, item.linkTarget.toUtf8().constData());
            const int rc = archive_write_header(a, entry);
            if (rc < ARCHIVE_WARN) {
                result->error = QStringLiteral("cannot add symlink %1: %2")
                                    .arg(item.relative, QString::fromUtf8(archive_error_string(a)));
                archive_entry_free(entry);
                failed = true;
                break;
            }
            archive_entry_free(entry);
            ++filesDone;
            reportProgress(item.relative, QStringLiteral("compressing"));
            continue;
        }

        if (item.isDir) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_size(entry, 0);
            const int rc = archive_write_header(a, entry);
            if (rc < ARCHIVE_WARN) {
                result->error = QStringLiteral("cannot add directory %1: %2")
                                    .arg(item.relative, QString::fromUtf8(archive_error_string(a)));
                archive_entry_free(entry);
                failed = true;
                break;
            }
            archive_entry_free(entry);
            continue;
        }

        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_size(entry, item.size);
        const int rc = archive_write_header(a, entry);
        archive_entry_free(entry);
        if (rc < ARCHIVE_WARN) {
            result->error = QStringLiteral("cannot add %1: %2")
                                .arg(item.relative, QString::fromUtf8(archive_error_string(a)));
            failed = true;
            break;
        }
        if (rc == ARCHIVE_WARN && !result->warnings.contains(QString::fromUtf8(archive_error_string(a))))
            result->warnings << QString::fromUtf8(archive_error_string(a));

        QFile file(item.absolute);
        if (!file.open(QIODevice::ReadOnly)) {
            result->error = QStringLiteral("cannot read %1: %2")
                                .arg(item.relative, file.errorString());
            failed = true;
            break;
        }

        QByteArray buffer(256 * 1024, Qt::Uninitialized);
        while (!file.atEnd()) {
            if (control && control->cancel.load()) {
                result->cancelled = true;
                break;
            }
            const qint64 n = file.read(buffer.data(), buffer.size());
            if (n < 0) {
                result->error = QStringLiteral("read error on %1").arg(item.relative);
                failed = true;
                break;
            }
            if (n == 0)
                break;
            const la_ssize_t written = archive_write_data(a, buffer.constData(),
                                                          static_cast<size_t>(n));
            if (written < 0) {
                result->error = QStringLiteral("write error on %1: %2")
                                    .arg(item.relative, QString::fromUtf8(archive_error_string(a)));
                failed = true;
                break;
            }
            bytesDone += n;
            if (lastEmit.elapsed() > 200) {
                reportProgress(item.relative, QStringLiteral("compressing"));
                lastEmit.restart();
            }
        }
        if (result->cancelled || failed)
            break;
        ++filesDone;
        reportProgress(item.relative, QStringLiteral("compressing"));
    }

    if (archive_write_close(a) != ARCHIVE_OK && !failed && !result->cancelled) {
        result->error = QStringLiteral("cannot finalize archive: %1")
                            .arg(QString::fromUtf8(archive_error_string(a)));
        failed = true;
    }
    archive_write_free(a);

    result->inputBytes = scan.totalBytes;
    result->filesAdded = filesDone;
    result->durationMs = timer.elapsed();
    if (result->cancelled) {
        result->error = QStringLiteral("cancelled");
        return false;
    }
    if (failed)
        return false;

    emit(QString(), QStringLiteral("finalizing"));
    return true;
}

bool runSevenZipCreate(const CompressionRequest &request, const ScanResult &scan,
                       const QString &tempPath, JobControl *control,
                       const CompressionEngine::ProgressCallback &progress,
                       CompressionResult *result)
{
    QString relativeError;
    const QString parent = commonParent(request.sources);
    if (parent.isEmpty()) {
        result->error = QStringLiteral("cannot determine common source directory");
        return false;
    }
    const QStringList relatives = relativeSources(request.sources, parent, &relativeError);
    if (relatives.isEmpty()) {
        result->error = relativeError.isEmpty() ? QStringLiteral("no sources") : relativeError;
        return false;
    }

    QStringList args;
    args << QStringLiteral("a")
         << QStringLiteral("-bd")
         << QStringLiteral("-bso1")
         << QStringLiteral("-bsp1")
         << QStringLiteral("-y");
    if (request.format == QLatin1String("zip"))
        args << QStringLiteral("-tzip");
    else
        args << QStringLiteral("-t7z");
    args << QStringLiteral("-mx=%1").arg(sevenZipLevel(request.level));
    args << QStringLiteral("-mmt=2");
    if (!request.password.isEmpty()) {
        args << (QStringLiteral("-p") + request.password);
        if (request.format == QLatin1String("7z") && request.encryptHeaders)
            args << QStringLiteral("-mhe=on");
        if (request.format == QLatin1String("zip"))
            args << QStringLiteral("-mem=AES256");
    }
    if (request.volumeSize > 0)
        args << (QStringLiteral("-v") + QString::number(request.volumeSize) + QStringLiteral("b"));
    args << tempPath;
    args << relatives;

    QProcess process;
    process.setProgram(CapabilityRegistry::sevenZipPath());
    process.setWorkingDirectory(parent);
    process.setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.start();
    if (!process.waitForStarted(5000)) {
        result->error = QStringLiteral("cannot start 7-Zip");
        return false;
    }
    process.closeWriteChannel();

    QElapsedTimer timer;
    timer.start();
    QRegularExpression percent(QStringLiteral("(\\d{1,3})%"));
    QString tail;
    bool paused = false;
    int lastPercent = -1;

    while (process.state() != QProcess::NotRunning) {
        if (control && control->cancel.load()) {
            process.terminate();
            if (!process.waitForFinished(2000))
                process.kill();
            process.waitForFinished(2000);
            result->cancelled = true;
            result->error = QStringLiteral("cancelled");
            return false;
        }
        if (control && control->pause.load() && !paused) {
            ::kill(static_cast<pid_t>(process.processId()), SIGSTOP);
            paused = true;
        } else if ((!control || !control->pause.load()) && paused) {
            ::kill(static_cast<pid_t>(process.processId()), SIGCONT);
            paused = false;
        }

        process.waitForReadyRead(200);
        const QString chunk = QString::fromLocal8Bit(process.readAllStandardOutput());
        if (!chunk.isEmpty()) {
            tail += chunk;
            if (tail.size() > 8192)
                tail = tail.right(4000);
            auto it = percent.globalMatch(chunk);
            while (it.hasNext()) {
                const int value = it.next().captured(1).toInt();
                lastPercent = value;
            }
            if (progress && lastPercent >= 0) {
                ProgressInfo p;
                p.bytesTotal = scan.totalBytes > 0 ? scan.totalBytes : -1;
                p.bytesDone = scan.totalBytes > 0
                                  ? scan.totalBytes * lastPercent / 100
                                  : 0;
                p.filesTotal = scan.totalFiles;
                p.filesDone = lastPercent * scan.totalFiles / 100;
                p.currentFile = QStringLiteral("compressing");
                p.status = QStringLiteral("compressing %1%").arg(lastPercent);
                p.elapsedMs = timer.elapsed();
                if (p.elapsedMs > 0 && p.bytesDone > 0)
                    p.bytesPerSecond = 1000.0 * static_cast<double>(p.bytesDone) / p.elapsedMs;
                progress(p);
            }
        }
    }

    if (paused)
        ::kill(static_cast<pid_t>(process.processId()), SIGCONT);

    const QString rest = QString::fromLocal8Bit(process.readAllStandardOutput())
                       + QString::fromLocal8Bit(process.readAllStandardError());
    if (!rest.trimmed().isEmpty())
        tail += rest;

    result->inputBytes = scan.totalBytes;
    result->filesAdded = scan.totalFiles;
    result->durationMs = timer.elapsed();

    const int code = process.exitCode();
    if (process.exitStatus() != QProcess::NormalExit || (code != 0 && code != 1)) {
        result->error = tail.trimmed().isEmpty()
                            ? QStringLiteral("7-Zip failed with exit code %1").arg(code)
                            : tail.trimmed().right(1500);
        return false;
    }
    if (code == 1) {
        const QString warning = tail.trimmed().right(500);
        if (!warning.isEmpty())
            result->warnings << warning;
    }
    if (!QFileInfo::exists(tempPath) && volumeSuffixesFor(tempPath).isEmpty()) {
        result->error = QStringLiteral("7-Zip did not create an archive");
        return false;
    }
    return true;
}

} // namespace

QString compressionLevelName(CompressionLevel level)
{
    switch (level) {
    case CompressionLevel::Store:
        return QStringLiteral("Store");
    case CompressionLevel::Fast:
        return QStringLiteral("Fast");
    case CompressionLevel::Normal:
        return QStringLiteral("Normal");
    case CompressionLevel::Maximum:
        return QStringLiteral("Maximum");
    }
    return QStringLiteral("Normal");
}

bool CompressionEngine::validate(const CompressionRequest &request, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (request.sources.isEmpty())
        return fail(QStringLiteral("no input files or directories selected"));
    if (request.outputPath.trimmed().isEmpty())
        return fail(QStringLiteral("no output archive specified"));
    if (request.format.isEmpty())
        return fail(QStringLiteral("no output format selected"));

    const FormatCapability capability = CapabilityRegistry::instance().format(request.format);
    if (!capability.canCreate)
        return fail(QStringLiteral("format cannot be created by the installed backends: %1")
                        .arg(request.format));
    if (!request.password.isEmpty() && !capability.canEncrypt)
        return fail(QStringLiteral("format does not support encryption: %1").arg(request.format));
    if (request.volumeSize > 0 && !capability.canMultiVolume)
        return fail(QStringLiteral("format does not support multi-volume archives: %1")
                        .arg(request.format));

    const QFileInfo output(request.outputPath);
    if (output.isDir())
        return fail(QStringLiteral("output path is a directory"));
    if (!output.absoluteDir().exists())
        return fail(QStringLiteral("output directory does not exist: %1")
                        .arg(output.absolutePath()));
    if (output.exists() && !request.overwrite)
        return fail(QStringLiteral("output already exists: %1").arg(request.outputPath));

    for (const QString &source : request.sources) {
        const QFileInfo info(source);
        if (!info.exists() && !info.isSymLink())
            return fail(QStringLiteral("input does not exist: %1").arg(source));
    }

    if (isStreamFormat(request.format)) {
        if (request.sources.size() != 1)
            return fail(QStringLiteral("%1 compression accepts exactly one input file")
                            .arg(capability.label));
        const QFileInfo info(request.sources.first());
        if (!info.isFile() || info.isSymLink())
            return fail(QStringLiteral("%1 compression accepts a regular file")
                            .arg(capability.label));
    }
    return true;
}

CompressionResult CompressionEngine::create(const CompressionRequest &request,
                                            JobControl *control,
                                            const ProgressCallback &progress)
{
    CompressionResult result;
    result.outputPath = request.outputPath;

    QString validationError;
    if (!validate(request, &validationError)) {
        result.error = validationError;
        return result;
    }

    const ScanResult scan = scanSources(request.sources);
    if (!scan.ok) {
        result.error = scan.error;
        return result;
    }
    if (scan.items.isEmpty()) {
        result.error = QStringLiteral("nothing to compress");
        return result;
    }
    result.warnings = scan.warnings;

    QString tempError;
    const QString tempPath = makeTempPath(request.outputPath, &tempError);
    if (tempPath.isEmpty()) {
        result.error = tempError;
        return result;
    }

    const bool sevenZip = usesSevenZip(request.format, request.password, request.volumeSize);
    result.backend = sevenZip ? QStringLiteral("7-Zip") : QStringLiteral("libarchive");
    result.encrypted = !request.password.isEmpty();

    bool created = false;
    if (sevenZip)
        created = runSevenZipCreate(request, scan, tempPath, control, progress, &result);
    else
        created = writeLibarchive(request, scan, tempPath, control, progress, &result);

    if (!created) {
        if (QFileInfo::exists(tempPath) || !volumeSuffixesFor(tempPath).isEmpty())
            cleanupVolumeSet(tempPath);
        if (result.error.isEmpty())
            result.error = result.cancelled ? QStringLiteral("cancelled")
                                            : QStringLiteral("compression failed");
        Logger::instance().warn(QStringLiteral("compression failed: %1 (%2)")
                                    .arg(request.outputPath, result.error));
        return result;
    }

    QString moveError;
    if (!moveOutputSet(tempPath, request.outputPath, request.overwrite, &moveError)) {
        cleanupVolumeSet(tempPath);
        result.error = moveError;
        return result;
    }

    if (result.encrypted) {
        // Encrypted archives default to owner-only permissions.
        ::chmod(QFile::encodeName(request.outputPath).constData(), 0600);
    }

    result.outputBytes = outputSetSize(request.outputPath);
    result.ok = true;

    if (progress) {
        ProgressInfo p;
        p.bytesDone = result.inputBytes;
        p.bytesTotal = result.inputBytes > 0 ? result.inputBytes : -1;
        p.filesDone = result.filesAdded;
        p.filesTotal = result.filesAdded;
        p.outputBytes = result.outputBytes;
        if (result.outputBytes > 0)
            p.compressionRatio = static_cast<double>(result.inputBytes)
                               / static_cast<double>(result.outputBytes);
        p.status = QStringLiteral("completed");
        p.elapsedMs = result.durationMs;
        progress(p);
    }

    Logger::instance().info(QStringLiteral("compression %1: %2 files, %3 -> %4, %5")
                                .arg(request.outputPath)
                                .arg(result.filesAdded)
                                .arg(utils::humanBytes(result.inputBytes),
                                     utils::humanBytes(result.outputBytes),
                                     result.backend));
    return result;
}

QString CompressionEngine::suggestOutputPath(const QStringList &sources, const QString &format)
{
    if (sources.isEmpty())
        return QString();

    const QString first = sources.first();
    const QFileInfo info(first);
    const QString dir = info.isDir() ? info.absolutePath() : info.absolutePath();
    QString base;
    if (sources.size() == 1)
        base = info.isDir() ? info.fileName() : utils::stripArchiveExtensions(info.fileName());
    else
        base = QStringLiteral("Archive");

    QString suffix = format;
    if (!CapabilityRegistry::instance().format(format).extensions.isEmpty())
        suffix = CapabilityRegistry::instance().format(format).extensions.first();

    QString candidate = dir + QLatin1Char('/') + base + QLatin1Char('.') + suffix;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base + QStringLiteral(" (%1).").arg(counter) + suffix;
        ++counter;
    }
    return candidate;
}

} // namespace eis
