#include "discimage/DiscImageEngine.h"

#include "archive/ArchiveService.h"
#include "core/Capabilities.h"
#include "core/Utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <sys/stat.h>

namespace eis {

namespace {

QString runTool(const QString &program, const QStringList &args, int timeoutMs,
                int *exitCode, QString *output)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.start();
    process.closeWriteChannel();
    if (!process.waitForStarted(5000)) {
        if (exitCode)
            *exitCode = -1;
        if (output)
            *output = QStringLiteral("cannot start %1").arg(QFileInfo(program).fileName());
        return QString();
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        if (exitCode)
            *exitCode = -2;
        if (output)
            *output = QStringLiteral("%1 timed out").arg(QFileInfo(program).fileName());
        return QString();
    }
    const QString text = QString::fromLocal8Bit(process.readAllStandardOutput())
                       + QString::fromLocal8Bit(process.readAllStandardError());
    if (exitCode)
        *exitCode = process.exitCode();
    if (output)
        *output = text;
    return text;
}

QString extensionUpper(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toUpper();
    return suffix.isEmpty() ? QStringLiteral("RAW") : suffix;
}

} // namespace

QStringList DiscImageEngine::knownExtensions()
{
    return {QStringLiteral("iso"), QStringLiteral("img"), QStringLiteral("bin"),
            QStringLiteral("cue"), QStringLiteral("mdf"), QStringLiteral("mds"),
            QStringLiteral("nrg"), QStringLiteral("dmg"), QStringLiteral("vhd"),
            QStringLiteral("vhdx"), QStringLiteral("raw")};
}

bool DiscImageEngine::looksLikeDiscImage(const QString &path)
{
    return knownExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool DiscImageEngine::canCreateIso()
{
    return CapabilityRegistry::xorrisoAvailable();
}

bool DiscImageEngine::canMount()
{
    return CapabilityRegistry::udisksctlAvailable();
}

QString DiscImageEngine::xorrisoVersion()
{
    const QString path = CapabilityRegistry::xorrisoPath();
    if (path.isEmpty())
        return QString();
    int code = 0;
    QString output;
    runTool(path, {QStringLiteral("-version")}, 10000, &code, &output);
    return output.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString DiscImageEngine::mountStatePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/EIS/eis-archive/mounts.json");
}

bool DiscImageEngine::isIsoMagic(const QString &path, QString *format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    if (!file.seek(32768))
        return false;
    const QByteArray header = file.read(8);
    if (header.size() >= 5 && header.mid(1, 5) == "CD001") {
        if (format)
            *format = QStringLiteral("ISO 9660");
        return true;
    }
    if (header.size() >= 5 && header.mid(1, 5) == "BEA01") {
        if (format)
            *format = QStringLiteral("UDF");
        return true;
    }
    // ISO 9660 descriptors may also start at 0x8000 for some hybrid images.
    if (file.seek(32769)) {
        const QByteArray alt = file.read(5);
        if (alt == "CD001") {
            if (format)
                *format = QStringLiteral("ISO 9660");
            return true;
        }
    }
    return false;
}

DiscImageInfo DiscImageEngine::inspect(const QString &path, const QString &password,
                                       int maxEntries) const
{
    DiscImageInfo info;
    info.path = path;
    info.format = extensionUpper(path);

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        info.error = QStringLiteral("image file does not exist");
        return info;
    }
    info.size = fileInfo.size();

    QString magicFormat;
    info.isoLike = isIsoMagic(path, &magicFormat);
    if (info.isoLike && !magicFormat.isEmpty())
        info.format = magicFormat;

    const QString suffix = fileInfo.suffix().toLower();
    const bool known = knownExtensions().contains(suffix);

    if (suffix == QLatin1String("cue") || suffix == QLatin1String("mds")
        || suffix == QLatin1String("mdf") || suffix == QLatin1String("nrg")) {
        info.readable = false;
        info.error = QStringLiteral("%1 images are not readable by the installed backends. "
                                    "Convert to ISO with a dedicated tool first.")
                         .arg(suffix.toUpper());
        return info;
    }
    if (suffix == QLatin1String("vhd") || suffix == QLatin1String("vhdx")) {
        info.readable = false;
        info.error = QStringLiteral("VHD/VHDX inspection is not supported by the installed "
                                    "libarchive/7-Zip build.");
        return info;
    }
    if (!info.isoLike && !known && suffix.isEmpty()) {
        info.error = QStringLiteral("unrecognized image format");
        return info;
    }

    ArchiveService service;
    info.listing = service.list(path, password, maxEntries, nullptr);
    info.readable = info.listing.ok;
    if (!info.listing.ok) {
        info.error = info.listing.error.isEmpty()
                         ? QStringLiteral("no backend can read this image")
                         : info.listing.error;
        return info;
    }
    // Keep the UDF/ISO 9660 magic label; backend listings only know the file
    // suffix (for example "iso").
    if (!info.isoLike && !info.listing.formatName.isEmpty())
        info.format = info.listing.formatName;
    info.mountable = info.isoLike;
    info.ok = true;
    return info;
}

bool DiscImageEngine::createIso(const QString &sourceDirectory, const QString &outputPath,
                                const QString &volumeLabel, QString *error,
                                JobControl *control) const
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!canCreateIso())
        return fail(QStringLiteral("xorriso is not installed; ISO creation is unavailable"));
    const QFileInfo source(sourceDirectory);
    if (!source.exists() || !source.isDir())
        return fail(QStringLiteral("source must be an existing directory"));
    if (outputPath.trimmed().isEmpty())
        return fail(QStringLiteral("output ISO path is empty"));
    const QFileInfo output(outputPath);
    if (output.exists())
        return fail(QStringLiteral("output already exists: %1").arg(outputPath));
    if (!output.absoluteDir().exists())
        return fail(QStringLiteral("output directory does not exist"));

    QString label = volumeLabel.trimmed().toUpper();
    label.remove(QRegularExpression(QStringLiteral("[^A-Z0-9_.-]")));
    if (label.size() > 32)
        label = label.left(32);
    if (label.isEmpty())
        label = QStringLiteral("EIS_IMAGE");

    QTemporaryFile temp(output.absoluteDir().filePath(QStringLiteral(".eis-iso-XXXXXX")));
    temp.setAutoRemove(false);
    if (!temp.open())
        return fail(QStringLiteral("cannot create temporary output in %1")
                        .arg(output.absolutePath()));
    const QString tempPath = temp.fileName();
    temp.close();
    temp.remove();

    QStringList args;
    args << QStringLiteral("-as") << QStringLiteral("mkisofs")
         << QStringLiteral("-o") << tempPath
         << QStringLiteral("-V") << label
         << QStringLiteral("-R") << QStringLiteral("-J")
         << source.absoluteFilePath();

    QProcess process;
    process.setProgram(CapabilityRegistry::xorrisoPath());
    process.setArguments(args);
    process.start();
    process.closeWriteChannel();
    if (!process.waitForStarted(5000)) {
        QFile::remove(tempPath);
        return fail(QStringLiteral("cannot start xorriso"));
    }

    bool cancelled = false;
    while (process.state() != QProcess::NotRunning) {
        if (control && control->cancel.load()) {
            process.terminate();
            if (!process.waitForFinished(2000))
                process.kill();
            cancelled = true;
            break;
        }
        process.waitForFinished(200);
    }
    if (cancelled) {
        process.waitForFinished(2000);
        QFile::remove(tempPath);
        return fail(QStringLiteral("cancelled"));
    }

    const QString toolOutput = QString::fromLocal8Bit(process.readAllStandardOutput())
                             + QString::fromLocal8Bit(process.readAllStandardError());
    if (process.exitCode() != 0 || !QFileInfo::exists(tempPath)) {
        QFile::remove(tempPath);
        return fail(toolOutput.trimmed().isEmpty()
                        ? QStringLiteral("xorriso failed")
                        : toolOutput.trimmed().right(1200));
    }

    if (!QFile::rename(tempPath, outputPath)) {
        QFile::remove(tempPath);
        return fail(QStringLiteral("cannot move ISO into place: %1").arg(outputPath));
    }
    ::chmod(QFile::encodeName(outputPath).constData(), 0644);
    return true;
}

QVector<MountRecord> DiscImageEngine::readMountState()
{
    QVector<MountRecord> records;
    QFile file(mountStatePath());
    if (!file.open(QIODevice::ReadOnly))
        return records;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    for (const QJsonValue &value : doc.object().value(QStringLiteral("mounts")).toArray()) {
        const QJsonObject o = value.toObject();
        MountRecord record;
        record.imagePath = o.value(QStringLiteral("imagePath")).toString();
        record.device = o.value(QStringLiteral("device")).toString();
        record.mountPoint = o.value(QStringLiteral("mountPoint")).toString();
        record.mountedAt = QDateTime::fromString(o.value(QStringLiteral("mountedAt")).toString(),
                                                 Qt::ISODate);
        if (!record.device.isEmpty())
            records.append(record);
    }
    return records;
}

bool DiscImageEngine::writeMountState(const QVector<MountRecord> &records)
{
    const QString path = mountStatePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    ::chmod(QFile::encodeName(QFileInfo(path).absolutePath()).constData(), 0700);

    QJsonArray array;
    for (const MountRecord &record : records) {
        QJsonObject o;
        o.insert(QStringLiteral("imagePath"), record.imagePath);
        o.insert(QStringLiteral("device"), record.device);
        o.insert(QStringLiteral("mountPoint"), record.mountPoint);
        o.insert(QStringLiteral("mountedAt"), record.mountedAt.toString(Qt::ISODate));
        array.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("mounts"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return false;
    ::chmod(QFile::encodeName(path).constData(), 0600);
    return true;
}

MountResult DiscImageEngine::mount(const QString &imagePath) const
{
    MountResult result;

    if (!canMount()) {
        result.error = QStringLiteral("udisksctl is not installed; mounting is unavailable");
        return result;
    }
    const QFileInfo info(imagePath);
    if (!info.exists() || !info.isFile()) {
        result.error = QStringLiteral("image file does not exist");
        return result;
    }

    const QString canonical = info.canonicalFilePath();
    for (const MountRecord &record : readMountState()) {
        if (record.imagePath == canonical && !record.device.isEmpty()) {
            result.ok = true;
            result.alreadyMounted = true;
            result.device = record.device;
            result.mountPoint = record.mountPoint;
            return result;
        }
    }

    QString format;
    if (!isIsoMagic(canonical, &format)) {
        result.error = QStringLiteral("only ISO 9660/UDF images can be mounted safely; "
                                      "use extraction for other image types");
        return result;
    }

    int code = 0;
    QString output;
    runTool(CapabilityRegistry::udisksctlPath(),
            {QStringLiteral("loop-setup"), QStringLiteral("--read-only"),
             QStringLiteral("--no-user-interaction"),
             QStringLiteral("-f"), canonical},
            30000, &code, &output);
    const QRegularExpression deviceExpression(QStringLiteral("(/dev/loop\\d+)"));
    const QRegularExpressionMatch match = deviceExpression.match(output);
    if (code != 0 || !match.hasMatch()) {
        result.error = output.trimmed().isEmpty()
                           ? QStringLiteral("udisksctl could not set up a loop device")
                           : output.trimmed().right(1200);
        return result;
    }
    const QString device = match.captured(1);

    QString mountOutput;
    runTool(CapabilityRegistry::udisksctlPath(),
            {QStringLiteral("mount"), QStringLiteral("-b"), device,
             QStringLiteral("--options"), QStringLiteral("ro"),
             QStringLiteral("--no-user-interaction")},
            30000, &code, &mountOutput);

    // The mount point may contain spaces (udisks uses the filesystem label),
    // for example "/run/media/user/DiRT 4". Capture the rest of the line.
    const QRegularExpression mountExpression(
        QStringLiteral("Mounted\\s+\\S+\\s+at\\s+(.+?)\\s*$"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch mountMatch = mountExpression.match(mountOutput);
    if (code != 0 || !mountMatch.hasMatch()) {
        // Do not leave an orphaned loop device behind.
        runTool(CapabilityRegistry::udisksctlPath(),
                {QStringLiteral("loop-delete"), QStringLiteral("-b"), device,
                 QStringLiteral("--no-user-interaction")},
                15000, nullptr, nullptr);
        result.error = mountOutput.trimmed().isEmpty()
                           ? QStringLiteral("device was mapped but could not be mounted")
                           : mountOutput.trimmed().right(1200);
        return result;
    }

    MountRecord record;
    record.imagePath = canonical;
    record.device = device;
    record.mountPoint = mountMatch.captured(1);
    record.mountedAt = QDateTime::currentDateTime();
    QVector<MountRecord> records = readMountState();
    records.append(record);
    writeMountState(records);

    result.ok = true;
    result.device = device;
    result.mountPoint = record.mountPoint;
    return result;
}

bool DiscImageEngine::unmount(const QString &imagePathOrDevice, QString *error) const
{
    QVector<MountRecord> records = readMountState();
    int index = -1;
    for (int i = 0; i < records.size(); ++i) {
        if (records[i].device == imagePathOrDevice) {
            index = i;
            break;
        }
        const QFileInfo info(records[i].imagePath);
        if (info.canonicalFilePath() == QFileInfo(imagePathOrDevice).canonicalFilePath()
            || records[i].imagePath == imagePathOrDevice
            || records[i].mountPoint == imagePathOrDevice) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QStringLiteral("no E.I.S-managed mount found for this image");
        return false;
    }

    const MountRecord record = records.at(index);
    int code = 0;
    QString output;
    runTool(CapabilityRegistry::udisksctlPath(),
            {QStringLiteral("unmount"), QStringLiteral("-b"), record.device,
             QStringLiteral("--no-user-interaction")},
            30000, &code, &output);
    if (code != 0) {
        if (error)
            *error = output.trimmed().isEmpty()
                         ? QStringLiteral("unmount failed")
                         : output.trimmed().right(1200);
        return false;
    }

    runTool(CapabilityRegistry::udisksctlPath(),
            {QStringLiteral("loop-delete"), QStringLiteral("-b"), record.device,
             QStringLiteral("--no-user-interaction")},
            15000, nullptr, nullptr);

    records.removeAt(index);
    writeMountState(records);
    return true;
}

QVector<MountRecord> DiscImageEngine::activeMounts() const
{
    return readMountState();
}

bool DiscImageEngine::unmountAll(QString *error) const
{
    bool ok = true;
    for (const MountRecord &record : readMountState()) {
        QString localError;
        if (!unmount(record.device, &localError)) {
            ok = false;
            if (error)
                *error = *error + (error->isEmpty() ? QString() : QStringLiteral("\n")) + localError;
        }
    }
    return ok;
}

} // namespace eis
