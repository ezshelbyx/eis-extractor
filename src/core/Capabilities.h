#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace eis {

// One format as actually supported by the installed backends. Capabilities
// are probed at runtime; nothing here is advertised without a backend check.
struct FormatCapability {
    QString id;            // canonical engine id: "zip", "tar.gz", ...
    QString label;         // human label
    QStringList extensions;
    bool canRead = false;
    bool canCreate = false;
    bool canEncrypt = false;
    bool canMultiVolume = false;
    QString readBackend;   // "libarchive", "7-Zip", ...
    QString writeBackend;
    QString note;          // limitations, shown verbatim in settings/help
};

class CapabilityRegistry {
public:
    static CapabilityRegistry &instance();

    QVector<FormatCapability> formats() const;
    FormatCapability format(const QString &id) const;

    bool canCreate(const QString &id) const;
    bool canEncrypt(const QString &id) const;
    QStringList creatableFormats() const;
    QStringList readableFormats() const;

    // Maps a file name or extension to a canonical format id. Returns an
    // empty string when the extension is unknown.
    QString formatForPath(const QString &path) const;

    // Capability registry plus backend versions, for the settings page.
    QString report() const;

    // Presence of optional system tools used by image/disc operations.
    static bool xorrisoAvailable();
    static bool udisksctlAvailable();
    static bool sevenZipAvailable();
    static QString xorrisoPath();
    static QString udisksctlPath();
    static QString sevenZipPath();

    // Format names reported by the installed 7-Zip, lower-cased. Empty when
    // 7-Zip is not installed.
    static QStringList sevenZipFormatNames();

private:
    CapabilityRegistry();
    QVector<FormatCapability> m_formats;
};

} // namespace eis
