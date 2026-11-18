#include "logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <exception>

namespace eis {

QString logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warning:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Success:
        return QStringLiteral("OK");
    }
    return QStringLiteral("INFO");
}

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger(QObject *parent)
    : QObject(parent)
{
    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/eis-extractor");
    QDir().mkpath(stateDir);
    m_path = stateDir + QStringLiteral("/eis-extractor.log");
    qRegisterMetaType<eis::LogEntry>("eis::LogEntry");
}

QString Logger::logFilePath() const
{
    return m_path;
}

void Logger::log(LogLevel level, const QString &text)
{
    LogEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.level = level;
    entry.text = text;

    {
        QMutexLocker locker(&m_mutex);
        m_history.append(entry);
        if (m_history.size() > 5000)
            m_history.remove(0, m_history.size() - 5000);
        appendFile(entry);
    }

    // Queued automatically when the receiver lives in another thread.
    emit message(entry);
}

void Logger::appendFile(const LogEntry &entry)
{
    if (m_path.isEmpty())
        return;

    if (!m_rotated) {
        const QFileInfo info(m_path);
        if (info.exists() && info.size() > 2LL * 1024 * 1024) {
            QFile::remove(m_path + QStringLiteral(".1"));
            QFile::rename(m_path, m_path + QStringLiteral(".1"));
        }
        m_rotated = true;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << entry.time.toString(Qt::ISODateWithMs) << ' '
           << logLevelName(entry.level) << ' '
           << entry.text << '\n';
}

QVector<LogEntry> Logger::history() const
{
    QMutexLocker locker(&m_mutex);
    return m_history;
}

} // namespace eis
