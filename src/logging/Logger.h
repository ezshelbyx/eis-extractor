#pragma once

#include "core/Types.h"

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QVector>

namespace eis {

QString logLevelName(LogLevel level);

class Logger : public QObject {
    Q_OBJECT
public:
    static Logger &instance();

    void log(LogLevel level, const QString &text);
    void debug(const QString &text) { log(LogLevel::Debug, text); }
    void info(const QString &text) { log(LogLevel::Info, text); }
    void warn(const QString &text) { log(LogLevel::Warning, text); }
    void error(const QString &text) { log(LogLevel::Error, text); }
    void success(const QString &text) { log(LogLevel::Success, text); }

    QVector<LogEntry> history() const;
    QString logFilePath() const;

signals:
    void message(const eis::LogEntry &entry);

private:
    explicit Logger(QObject *parent = nullptr);

    void appendFile(const LogEntry &entry);

    mutable QMutex m_mutex;
    QVector<LogEntry> m_history;
    QString m_path;
    bool m_rotated = false;
};

} // namespace eis
