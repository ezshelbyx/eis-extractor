#pragma once

#include "vault/VaultCrypto.h"

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace eis {

struct PasswordHistoryItem {
    QString password;
    QDateTime changedAt;
};

struct VaultEntry {
    QString id;
    QString type = QStringLiteral("website");
    QString title;
    QString username;
    QString password;
    QString url;
    QString email;
    QString notes;
    QString category;
    QStringList tags;
    bool favorite = false;
    QMap<QString, QString> customFields;
    QVector<PasswordHistoryItem> history;
    QDateTime created;
    QDateTime modified;
};

struct ArchivePasswordRecord {
    QString id;
    QString archiveName;
    QString archivePath;
    QString password;
    QString group;
    QString note;
    QDateTime created;
    QDateTime modified;
};

// Encrypted vault: Argon2id-derived key, AES-256-GCM authenticated payload,
// versioned header, atomic 0600 writes. The master password is never stored.
class PasswordVault : public QObject {
    Q_OBJECT
public:
    explicit PasswordVault(QObject *parent = nullptr);
    ~PasswordVault() override;

    // Shared unlocked-session vault used by the UI (password lookup from the
    // extraction page and the vault page itself).
    static PasswordVault &instance();

    static QString defaultVaultPath();
    static QStringList entryTypes();

    static bool isValidEntryType(const QString &type);
    static QString entryTypeLabel(const QString &type);

    bool vaultExists(const QString &path = QString()) const;
    bool create(const QString &path, const QString &masterPassword, QString *error);
    bool unlock(const QString &path, const QString &masterPassword, QString *error);
    bool unlock(const QString &masterPassword, QString *error);
    bool save(QString *error);
    void lock();

    bool isUnlocked() const { return m_unlocked; }
    QString path() const { return m_path; }

    QVector<VaultEntry> entries() const { return m_entries; }
    QVector<ArchivePasswordRecord> archivePasswords() const { return m_archivePasswords; }
    int entryCount() const { return m_entries.size(); }
    int archivePasswordCount() const { return m_archivePasswords.size(); }

    bool addEntry(VaultEntry entry, QString *error);
    bool updateEntry(const VaultEntry &entry, QString *error);
    bool removeEntry(const QString &id, QString *error);

    bool addArchivePassword(ArchivePasswordRecord record, QString *error);
    bool updateArchivePassword(const ArchivePasswordRecord &record, QString *error);
    bool removeArchivePassword(const QString &id, QString *error);

    QVector<VaultEntry> searchEntries(const QString &query,
                                      const QString &category = QString(),
                                      bool favoritesOnly = false) const;
    QStringList categories() const;

    // Matches exact archive path first, then file name. Multiple records per
    // archive are intentionally allowed.
    QVector<ArchivePasswordRecord> passwordsForArchive(const QString &archivePath) const;

    bool exportEncrypted(const QString &destination, QString *error) const;
    bool importEncrypted(const QString &source, const QString &sourceMasterPassword,
                         int *importedEntries, QString *error);
    bool restoreEncrypted(const QString &source, const QString &sourceMasterPassword,
                          QString *error);

signals:
    void changed();
    void locked();

private:
    struct Decoded {
        QVector<VaultEntry> entries;
        QVector<ArchivePasswordRecord> archivePasswords;
    };

    bool writeTo(const QString &destination, QString *error) const;
    bool readFrom(const QString &source, const QString &masterPassword,
                  Decoded *decoded, QString *error,
                  QByteArray *keyOut = nullptr,
                  QByteArray *saltOut = nullptr,
                  KdfParams *kdfOut = nullptr) const;
    QByteArray serializeJson() const;
    bool deserializeJson(const QByteArray &json, Decoded *decoded, QString *error) const;
    void wipeState();

    QString m_path;
    QByteArray m_key;  // 32 bytes while unlocked
    QByteArray m_salt;
    KdfParams m_kdf;
    bool m_unlocked = false;
    QVector<VaultEntry> m_entries;
    QVector<ArchivePasswordRecord> m_archivePasswords;
};

} // namespace eis
