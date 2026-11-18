#include "settings/AppSettings.h"

#include <QDir>
#include <QStandardPaths>

namespace eis {

AppSettings &AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
    : m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("EIS"), QStringLiteral("eis-extractor"))
{
}

QString AppSettings::theme() const
{
    const QString value = m_settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    return value == QLatin1String("light") ? value : QStringLiteral("dark");
}

void AppSettings::setTheme(const QString &theme)
{
    m_settings.setValue(QStringLiteral("ui/theme"), theme);
    m_settings.sync();
    emit changed();
}

bool AppSettings::compactMode() const
{
    return m_settings.value(QStringLiteral("ui/compact"), false).toBool();
}

void AppSettings::setCompactMode(bool compact)
{
    m_settings.setValue(QStringLiteral("ui/compact"), compact);
    m_settings.sync();
    emit changed();
}

bool AppSettings::reducedMotion() const
{
    return m_settings.value(QStringLiteral("ui/reducedMotion"), false).toBool();
}

void AppSettings::setReducedMotion(bool reduced)
{
    m_settings.setValue(QStringLiteral("ui/reducedMotion"), reduced);
    m_settings.sync();
    emit changed();
}

int AppSettings::maxConcurrent() const
{
    return qBound(1, m_settings.value(QStringLiteral("extraction/maxConcurrent"), 2).toInt(), 4);
}

void AppSettings::setMaxConcurrent(int value)
{
    m_settings.setValue(QStringLiteral("extraction/maxConcurrent"), qBound(1, value, 4));
    m_settings.sync();
    emit changed();
}

QString AppSettings::collisionPolicy() const
{
    const QString value = m_settings.value(QStringLiteral("extraction/collision"),
                                           QStringLiteral("rename")).toString();
    if (value == QLatin1String("skip") || value == QLatin1String("overwrite"))
        return value;
    return QStringLiteral("rename");
}

void AppSettings::setCollisionPolicy(const QString &policy)
{
    m_settings.setValue(QStringLiteral("extraction/collision"), policy);
    m_settings.sync();
    emit changed();
}

qint64 AppSettings::maxEntries() const
{
    return m_settings.value(QStringLiteral("limits/maxEntries"), 1000000LL).toLongLong();
}

void AppSettings::setMaxEntries(qint64 value)
{
    m_settings.setValue(QStringLiteral("limits/maxEntries"), value);
    m_settings.sync();
    emit changed();
}

qint64 AppSettings::maxTotalBytes() const
{
    return m_settings.value(QStringLiteral("limits/maxTotalBytes"), 200LL * 1024 * 1024 * 1024).toLongLong();
}

void AppSettings::setMaxTotalBytes(qint64 value)
{
    m_settings.setValue(QStringLiteral("limits/maxTotalBytes"), value);
    m_settings.sync();
    emit changed();
}

bool AppSettings::preservePermissions() const
{
    return m_settings.value(QStringLiteral("extraction/preservePermissions"), true).toBool();
}

void AppSettings::setPreservePermissions(bool preserve)
{
    m_settings.setValue(QStringLiteral("extraction/preservePermissions"), preserve);
    m_settings.sync();
    emit changed();
}

QString AppSettings::lastDirectory() const
{
    const QString value = m_settings.value(QStringLiteral("paths/lastDirectory")).toString();
    if (!value.isEmpty() && QDir(value).exists())
        return value;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void AppSettings::setLastDirectory(const QString &path)
{
    m_settings.setValue(QStringLiteral("paths/lastDirectory"), path);
    m_settings.sync();
}

void AppSettings::resetDefaults()
{
    m_settings.clear();
    m_settings.sync();
    emit changed();
}

} // namespace eis
