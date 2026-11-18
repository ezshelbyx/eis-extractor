#include "core/Utils.h"
#include "core/Types.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <cmath>

namespace eis::utils {

QString humanBytes(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("--");
    const double value = static_cast<double>(bytes);
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double scaled = value;
    int unit = 0;
    while (scaled >= 1024.0 && unit < 5) {
        scaled /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(scaled, 0, 'f', scaled < 10.0 ? 2 : 1).arg(units[unit]);
}

QString humanSpeed(double bytesPerSecond)
{
    if (!std::isfinite(bytesPerSecond) || bytesPerSecond <= 0.0)
        return QStringLiteral("--");
    return humanBytes(static_cast<qint64>(bytesPerSecond)) + QStringLiteral("/s");
}

QString humanDuration(qint64 milliseconds)
{
    if (milliseconds < 0)
        return QStringLiteral("--");
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'));
    if (minutes > 0)
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1s").arg(seconds);
}

QString percentString(qint64 done, qint64 total)
{
    if (total <= 0)
        return QStringLiteral("--");
    const double pct = 100.0 * static_cast<double>(done) / static_cast<double>(total);
    return QStringLiteral("%1%").arg(pct, 0, 'f', 1);
}

namespace {
const QStringList kDoubleExtensions = {
    QStringLiteral("tar.gz"), QStringLiteral("tar.bz2"), QStringLiteral("tar.xz"),
    QStringLiteral("tar.zst"), QStringLiteral("tar.lz4"), QStringLiteral("tar.lzma"),
    QStringLiteral("tar.lz"), QStringLiteral("tar.z"), QStringLiteral("tar.br"),
    QStringLiteral("tar.gr"), QStringLiteral("tar.lrz")
};
const QStringList kSingleExtensions = {
    QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("tar"),
    QStringLiteral("gz"), QStringLiteral("xz"), QStringLiteral("bz2"), QStringLiteral("zst"),
    QStringLiteral("lz4"), QStringLiteral("lzma"), QStringLiteral("lz"), QStringLiteral("z"),
    QStringLiteral("iso"), QStringLiteral("cab"), QStringLiteral("cpio"), QStringLiteral("ar"),
    QStringLiteral("deb"), QStringLiteral("rpm"), QStringLiteral("lha"), QStringLiteral("lzh"),
    QStringLiteral("xar"), QStringLiteral("warc"), QStringLiteral("br"), QStringLiteral("zoo")
};
} // namespace

QString stripArchiveExtensions(const QString &fileName)
{
    QString name = fileName;
    const QString lower = name.toLower();
    for (const QString &ext : kDoubleExtensions) {
        if (lower.endsWith(QLatin1Char('.') + ext)) {
            name.chop(ext.size() + 1);
            return name;
        }
    }
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        const QString ext = name.mid(dot + 1).toLower();
        if (kSingleExtensions.contains(ext))
            name.chop(name.size() - dot);
    }
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return QStringLiteral("extracted");
    return name;
}

QString archiveBaseName(const QString &path)
{
    return stripArchiveExtensions(QFileInfo(path).fileName());
}

QString uniqueDirectory(const QString &desired)
{
    if (!QFileInfo::exists(desired) && !QFileInfo(desired).isSymLink())
        return desired;
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = desired + QStringLiteral(" (%1)").arg(i);
        if (!QFileInfo::exists(candidate) && !QFileInfo(candidate).isSymLink())
            return candidate;
    }
    return desired + QStringLiteral(" (copy)");
}

QStringList archiveExtensions()
{
    QStringList exts;
    for (const QString &e : kSingleExtensions)
        exts << e;
    for (const QString &e : kDoubleExtensions)
        exts << e;
    return exts;
}

QString archiveNameFilter()
{
    QStringList globs;
    const QStringList exts = archiveExtensions();
    for (const QString &ext : exts)
        globs << QStringLiteral("*.") + ext;
    return QStringLiteral("Archives (%1);;All files (*)").arg(globs.join(QLatin1Char(' ')));
}

bool looksLikeArchive(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    for (const QString &ext : kDoubleExtensions) {
        if (name.endsWith(QLatin1Char('.') + ext))
            return true;
    }
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return false;
    return kSingleExtensions.contains(name.mid(dot + 1));
}

} // namespace eis::utils

namespace eis {

QString jobStateLabel(JobState state)
{
    switch (state) {
    case JobState::Queued:
        return QStringLiteral("QUEUED");
    case JobState::Running:
        return QStringLiteral("RUNNING");
    case JobState::Paused:
        return QStringLiteral("PAUSED");
    case JobState::Completed:
        return QStringLiteral("COMPLETED");
    case JobState::Failed:
        return QStringLiteral("FAILED");
    case JobState::Cancelled:
        return QStringLiteral("CANCELLED");
    }
    return QStringLiteral("UNKNOWN");
}

} // namespace eis
