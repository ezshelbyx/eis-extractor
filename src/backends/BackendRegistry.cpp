#include "backends/BackendRegistry.h"

#include "backends/LibarchiveBackend.h"
#include "backends/SevenZipBackend.h"

#include <QProcess>
#include <QStandardPaths>

namespace eis {

BackendRegistry::BackendRegistry()
{
    m_libarchive = std::make_shared<LibarchiveBackend>();
    m_sevenzip = std::make_shared<SevenZipBackend>();
}

std::vector<std::shared_ptr<ArchiveBackend>> BackendRegistry::backends() const
{
    std::vector<std::shared_ptr<ArchiveBackend>> result;
    result.push_back(m_libarchive);
    if (m_sevenzip->available())
        result.push_back(m_sevenzip);
    return result;
}

std::shared_ptr<ArchiveBackend> BackendRegistry::primary() const
{
    return m_libarchive;
}

std::shared_ptr<ArchiveBackend> BackendRegistry::fallback() const
{
    return m_sevenzip->available() ? m_sevenzip : nullptr;
}

std::shared_ptr<ArchiveBackend> BackendRegistry::select(const QString &archivePath,
                                                        const QString &password,
                                                        QString *formatOut,
                                                        QString *errorOut) const
{
    QString format, error;
    if (m_libarchive->probe(archivePath, password, &format, &error)) {
        if (formatOut)
            *formatOut = format;
        return m_libarchive;
    }
    if (m_sevenzip->available() && m_sevenzip->probe(archivePath, password, &format, &error)) {
        if (formatOut)
            *formatOut = format;
        return m_sevenzip;
    }
    if (errorOut)
        *errorOut = error.isEmpty() ? QStringLiteral("no backend can handle this archive") : error;
    return nullptr;
}

QStringList BackendRegistry::supportedFormats() const
{
    QStringList formats;
    for (const auto &backend : backends())
        for (const QString &f : backend->formats())
            if (!formats.contains(f, Qt::CaseInsensitive))
                formats << f;
    formats.sort(Qt::CaseInsensitive);
    return formats;
}

QString BackendRegistry::capabilityReport() const
{
    QStringList lines;
    lines << QStringLiteral("libarchive: %1").arg(m_libarchive->available() ? QStringLiteral("available") : QStringLiteral("unavailable"));
    lines << QStringLiteral("  formats: %1").arg(m_libarchive->formats().join(QStringLiteral(", ")));
    if (m_sevenzip->available()) {
        lines << QStringLiteral("7-Zip: available (%1)").arg(m_sevenzip->executablePath());
        lines << QStringLiteral("  formats: %1").arg(m_sevenzip->formats().join(QStringLiteral(", ")));
    } else {
        lines << QStringLiteral("7-Zip: not installed (optional fallback disabled)");
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace eis
