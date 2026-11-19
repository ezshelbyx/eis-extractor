#pragma once

#include "core/Types.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace eis {

struct DiscImageInfo {
    bool ok = false;
    bool readable = false;
    bool mountable = false;
    bool isoLike = false;
    QString path;
    QString format;
    QString error;
    qint64 size = 0;
    ArchiveListing listing;
};

struct MountRecord {
    QString imagePath;
    QString device;
    QString mountPoint;
    QDateTime mountedAt;
};

struct MountResult {
    bool ok = false;
    bool alreadyMounted = false;
    QString device;
    QString mountPoint;
    QString error;
};

// Disc images are handled with libarchive/7-Zip for reading and xorriso for
// ISO creation. Mounting is ISO-only, read-only, through udisks2/polkit; the
// application itself never runs as root.
class DiscImageEngine {
public:
    static QStringList knownExtensions();
    static bool looksLikeDiscImage(const QString &path);

    static bool canCreateIso();
    static bool canMount();
    static QString xorrisoVersion();
    static QString mountStatePath();

    // Detects the real container by magic where possible, then probes the
    // archive backend. Never claims a format the backend cannot read.
    DiscImageInfo inspect(const QString &path, const QString &password = QString(),
                          int maxEntries = 100000) const;

    bool createIso(const QString &sourceDirectory, const QString &outputPath,
                   const QString &volumeLabel, QString *error,
                   JobControl *control = nullptr) const;

    MountResult mount(const QString &imagePath) const;
    bool unmount(const QString &imagePathOrDevice, QString *error) const;
    QVector<MountRecord> activeMounts() const;
    bool unmountAll(QString *error = nullptr) const;

private:
    static bool isIsoMagic(const QString &path, QString *format);
    static QVector<MountRecord> readMountState();
    static bool writeMountState(const QVector<MountRecord> &records);
};

} // namespace eis
