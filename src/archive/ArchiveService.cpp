#include "archive/ArchiveService.h"

#include "backends/SevenZipBackend.h"
#include "core/Utils.h"
#include "logging/Logger.h"
#include "security/PathGuard.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <archive.h>
#include <archive_entry.h>

namespace eis {

ArchiveService::ArchiveService() = default;

ArchiveListing ArchiveService::list(const QString &archivePath, const QString &password,
                                   int maxEntries, const std::atomic<bool> *cancel)
{
    QString format, error;
    auto backend = m_registry.select(archivePath, password, &format, &error);
    if (!backend) {
        ArchiveListing l;
        l.backendName = QStringLiteral("none");
        l.error = error.isEmpty() ? QStringLiteral("no backend available") : error;
        return l;
    }
    ArchiveListing l = backend->list(archivePath, password, maxEntries, cancel, nullptr);
    if (l.formatName.isEmpty())
        l.formatName = format;
    if (l.backendName.isEmpty())
        l.backendName = backend->name();
    return l;
}

QString ArchiveService::detectFormat(const QString &archivePath, const QString &password) const
{
    QString format, error;
    // const_cast needed because select is non-const logical; registry is mutable here.
    // Use a local copy approach instead.
    BackendRegistry registry;
    auto backend = registry.select(archivePath, password, &format, &error);
    Q_UNUSED(backend)
    if (format.isEmpty())
        return QFileInfo(archivePath).suffix().toLower();
    return format;
}

QString ArchiveService::sha256File(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(65536);
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

IntegrityResult ArchiveService::test(const QString &archivePath, const QString &password)
{
    IntegrityResult result;
    result.sha256 = sha256File(archivePath);

    QElapsedTimer timer;
    timer.start();

    QString format, error;
    BackendRegistry registry;
    auto backend = registry.select(archivePath, password, &format, &error);
    if (!backend) {
        result.error = error.isEmpty() ? QStringLiteral("no backend available") : error;
        result.durationMs = timer.elapsed();
        return result;
    }
    result.backendName = backend->name();
    result.formatName = format.isEmpty() ? QFileInfo(archivePath).suffix().toLower() : format;

    // Prefer 7-Zip's native test command when 7-Zip is the selected backend.
    if (backend->kind() == BackendKind::SevenZip) {
        SevenZipBackend *sz = static_cast<SevenZipBackend *>(backend.get());
        Q_UNUSED(sz)
        QStringList args;
        if (!password.isEmpty())
            args << (QStringLiteral("-p") + password);
        else
            args << QStringLiteral("-p");
        args << QStringLiteral("t") << QStringLiteral("--") << archivePath;
        // Use QProcess via SevenZipBackend? Reimplement here.
        QProcess process;
        process.setProgram(backend->executablePath());
        process.setArguments(args);
        process.start();
        process.closeWriteChannel();
        if (!process.waitForFinished(300000)) {
            process.kill();
            process.waitForFinished(3000);
            result.error = QStringLiteral("integrity test timed out");
            result.durationMs = timer.elapsed();
            return result;
        }
        const QString out = QString::fromLocal8Bit(process.readAllStandardOutput())
                          + QString::fromLocal8Bit(process.readAllStandardError());
        result.completed = true;
        result.ok = (process.exitCode() == 0);
        if (!result.ok)
            result.error = out.trimmed().isEmpty() ? QStringLiteral("integrity test failed") : out.trimmed();
        result.durationMs = timer.elapsed();
        return result;
    }

    // libarchive path: read every entry's data to validate CRCs.
    archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (!password.isEmpty())
        archive_read_add_passphrase(a, password.toUtf8().constData());

    if (archive_read_open_filename(a, QFile::encodeName(archivePath).constData(), 10240) != ARCHIVE_OK) {
        const QString msg = QString::fromUtf8(archive_error_string(a));
        archive_read_free(a);
        result.error = msg.isEmpty() ? QStringLiteral("cannot open archive") : msg;
        result.durationMs = timer.elapsed();
        return result;
    }

    archive_entry *entry = nullptr;
    bool failed = false;
    QString failMsg;
    qint64 bytesRead = 0;
    while (true) {
        const int rc = archive_read_next_header(a, &entry);
        if (rc == ARCHIVE_EOF)
            break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            failed = true;
            failMsg = QString::fromUtf8(archive_error_string(a));
            break;
        }
        if (rc == ARCHIVE_WARN) {
            const QString msg = QString::fromUtf8(archive_error_string(a));
            if (msg.contains(QStringLiteral("CRC"), Qt::CaseInsensitive)
                || msg.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
                failed = true;
                failMsg = msg;
                break;
            }
        }

        const void *buff = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (true) {
            const int r = archive_read_data_block(a, &buff, &size, &offset);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
                failed = true;
                failMsg = QString::fromUtf8(archive_error_string(a));
                break;
            }
            if (r == ARCHIVE_WARN) {
                const QString msg = QString::fromUtf8(archive_error_string(a));
                if (msg.contains(QStringLiteral("CRC"), Qt::CaseInsensitive)
                    || msg.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
                    failed = true;
                    failMsg = msg;
                    break;
                }
            }
            bytesRead += (qint64)size;
        }
        if (failed)
            break;
    }

    archive_read_close(a);
    archive_read_free(a);

    result.completed = true;
    result.bytesRead = bytesRead;
    result.ok = !failed;
    if (failed)
        result.error = failMsg.isEmpty() ? QStringLiteral("integrity test failed") : failMsg;
    result.durationMs = timer.elapsed();
    return result;
}

} // namespace eis
