#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

namespace eis {

class AppSettings : public QObject {
    Q_OBJECT
public:
    static AppSettings &instance();

    QString theme() const;              // "dark" | "light"
    void setTheme(const QString &theme);

    bool compactMode() const;
    void setCompactMode(bool compact);

    bool reducedMotion() const;
    void setReducedMotion(bool reduced);

    int maxConcurrent() const;
    void setMaxConcurrent(int value);

    QString collisionPolicy() const;    // "rename" | "skip" | "overwrite"
    void setCollisionPolicy(const QString &policy);

    qint64 maxEntries() const;
    void setMaxEntries(qint64 value);

    qint64 maxTotalBytes() const;
    void setMaxTotalBytes(qint64 value);

    bool preservePermissions() const;
    void setPreservePermissions(bool preserve);

    QString lastDirectory() const;
    void setLastDirectory(const QString &path);

    void resetDefaults();

signals:
    void changed();

private:
    AppSettings();
    QSettings m_settings;
};

} // namespace eis
