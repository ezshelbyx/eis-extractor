#include "vault/PasswordVault.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <sys/stat.h>

namespace eis {

namespace {

const char kMagic[] = "EISVLT01";
constexpr int kSaltSize = 16;
constexpr int kNonceSize = 12;
constexpr int kTagSize = 16;
constexpr qint64 kMaxVaultFileSize = 64LL * 1024 * 1024;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QDateTime parseDate(const QJsonValue &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QString dateString(const QDateTime &value)
{
    return value.isValid() ? value.toString(Qt::ISODateWithMs) : QString();
}

void wipe(QString *text)
{
    if (!text)
        return;
    text->fill(QChar('\0'));
    text->clear();
}

QJsonObject entryToJson(const VaultEntry &entry)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), entry.id);
    o.insert(QStringLiteral("type"), entry.type);
    o.insert(QStringLiteral("title"), entry.title);
    o.insert(QStringLiteral("username"), entry.username);
    o.insert(QStringLiteral("password"), entry.password);
    o.insert(QStringLiteral("url"), entry.url);
    o.insert(QStringLiteral("email"), entry.email);
    o.insert(QStringLiteral("notes"), entry.notes);
    o.insert(QStringLiteral("category"), entry.category);
    o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(entry.tags));
    o.insert(QStringLiteral("favorite"), entry.favorite);
    o.insert(QStringLiteral("created"), dateString(entry.created));
    o.insert(QStringLiteral("modified"), dateString(entry.modified));

    if (!entry.customFields.isEmpty()) {
        QJsonObject custom;
        for (auto it = entry.customFields.constBegin(); it != entry.customFields.constEnd(); ++it)
            custom.insert(it.key(), it.value());
        o.insert(QStringLiteral("customFields"), custom);
    }
    if (!entry.history.isEmpty()) {
        QJsonArray history;
        for (const PasswordHistoryItem &item : entry.history) {
            QJsonObject h;
            h.insert(QStringLiteral("password"), item.password);
            h.insert(QStringLiteral("changedAt"), dateString(item.changedAt));
            history.append(h);
        }
        o.insert(QStringLiteral("history"), history);
    }
    return o;
}

QJsonObject archivePasswordToJson(const ArchivePasswordRecord &record)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), record.id);
    o.insert(QStringLiteral("archiveName"), record.archiveName);
    o.insert(QStringLiteral("archivePath"), record.archivePath);
    o.insert(QStringLiteral("password"), record.password);
    o.insert(QStringLiteral("group"), record.group);
    o.insert(QStringLiteral("note"), record.note);
    o.insert(QStringLiteral("created"), dateString(record.created));
    o.insert(QStringLiteral("modified"), dateString(record.modified));
    return o;
}

} // namespace

PasswordVault::PasswordVault(QObject *parent)
    : QObject(parent)
{
}

PasswordVault::~PasswordVault()
{
    lock();
}

PasswordVault &PasswordVault::instance()
{
    static PasswordVault vault;
    return vault;
}

QString PasswordVault::defaultVaultPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/EIS/eis-archive/vault.eisvault");
}

QStringList PasswordVault::entryTypes()
{
    return {QStringLiteral("website"), QStringLiteral("application"), QStringLiteral("email"),
            QStringLiteral("system"), QStringLiteral("wifi"), QStringLiteral("server"),
            QStringLiteral("ssh"), QStringLiteral("license"), QStringLiteral("note"),
            QStringLiteral("custom")};
}

bool PasswordVault::isValidEntryType(const QString &type)
{
    return entryTypes().contains(type.toLower());
}

QString PasswordVault::entryTypeLabel(const QString &type)
{
    const QString lower = type.toLower();
    if (lower == QLatin1String("website"))
        return QStringLiteral("Website account");
    if (lower == QLatin1String("application"))
        return QStringLiteral("Application account");
    if (lower == QLatin1String("email"))
        return QStringLiteral("Email account");
    if (lower == QLatin1String("system"))
        return QStringLiteral("System credential");
    if (lower == QLatin1String("wifi"))
        return QStringLiteral("Wi-Fi credential");
    if (lower == QLatin1String("server"))
        return QStringLiteral("Server credential");
    if (lower == QLatin1String("ssh"))
        return QStringLiteral("SSH credential");
    if (lower == QLatin1String("license"))
        return QStringLiteral("Software license");
    if (lower == QLatin1String("note"))
        return QStringLiteral("Secure note");
    if (lower == QLatin1String("custom"))
        return QStringLiteral("Custom entry");
    return type;
}

bool PasswordVault::vaultExists(const QString &path) const
{
    return QFileInfo::exists(path.isEmpty() ? m_path : path);
}

bool PasswordVault::create(const QString &path, const QString &masterPassword, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (path.trimmed().isEmpty())
        return fail(QStringLiteral("vault path is empty"));
    if (masterPassword.size() < 8)
        return fail(QStringLiteral("master password must be at least 8 characters"));
    if (QFileInfo::exists(path))
        return fail(QStringLiteral("a vault already exists at this location"));
    if (QFileInfo(path).isDir())
        return fail(QStringLiteral("vault path is a directory"));

    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath()))
        return fail(QStringLiteral("cannot create vault directory: %1").arg(dir.absolutePath()));
    ::chmod(QFile::encodeName(dir.absolutePath()).constData(), 0700);

    lock();
    m_path = path;
    m_kdf = KdfParams();
    m_salt = VaultCrypto::randomBytes(kSaltSize);
    if (m_salt.size() != kSaltSize)
        return fail(QStringLiteral("secure random generator unavailable"));
    if (!VaultCrypto::deriveKey(masterPassword.toUtf8(), m_salt, m_kdf, &m_key, error)) {
        m_path.clear();
        return false;
    }
    m_entries.clear();
    m_archivePasswords.clear();
    m_unlocked = true;

    if (!writeTo(path, error)) {
        wipeState();
        m_path.clear();
        return false;
    }
    emit changed();
    return true;
}

bool PasswordVault::unlock(const QString &path, const QString &masterPassword, QString *error)
{
    if (masterPassword.isEmpty()) {
        if (error)
            *error = QStringLiteral("master password is required");
        return false;
    }
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("vault file does not exist: %1").arg(path);
        return false;
    }

    Decoded decoded;
    QByteArray key;
    QByteArray salt;
    KdfParams kdf;
    if (!readFrom(path, masterPassword, &decoded, error, &key, &salt, &kdf))
        return false;

    lock();
    m_path = path;
    m_entries = decoded.entries;
    m_archivePasswords = decoded.archivePasswords;
    m_key = key;
    m_salt = salt;
    m_kdf = kdf;
    m_unlocked = true;
    emit changed();
    return true;
}

bool PasswordVault::unlock(const QString &masterPassword, QString *error)
{
    if (m_path.isEmpty()) {
        if (error)
            *error = QStringLiteral("no vault path has been selected");
        return false;
    }
    return unlock(m_path, masterPassword, error);
}

bool PasswordVault::save(QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    return writeTo(m_path, error);
}

void PasswordVault::lock()
{
    wipeState();
    emit locked();
}

QByteArray PasswordVault::serializeJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("eis-vault"));
    root.insert(QStringLiteral("version"), 1);

    QJsonArray entries;
    for (const VaultEntry &entry : m_entries)
        entries.append(entryToJson(entry));
    root.insert(QStringLiteral("entries"), entries);

    QJsonArray archivePasswords;
    for (const ArchivePasswordRecord &record : m_archivePasswords)
        archivePasswords.append(archivePasswordToJson(record));
    root.insert(QStringLiteral("archivePasswords"), archivePasswords);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool PasswordVault::deserializeJson(const QByteArray &json, Decoded *decoded, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("vault payload is not valid JSON");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("eis-vault")) {
        if (error)
            *error = QStringLiteral("unrecognized vault format");
        return false;
    }
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version != 1) {
        if (error)
            *error = QStringLiteral("unsupported vault version: %1").arg(version);
        return false;
    }

    Decoded result;
    for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject o = value.toObject();
        VaultEntry entry;
        entry.id = o.value(QStringLiteral("id")).toString();
        if (entry.id.isEmpty())
            entry.id = newId();
        entry.type = o.value(QStringLiteral("type")).toString(QStringLiteral("custom"));
        if (!isValidEntryType(entry.type))
            entry.type = QStringLiteral("custom");
        entry.title = o.value(QStringLiteral("title")).toString();
        entry.username = o.value(QStringLiteral("username")).toString();
        entry.password = o.value(QStringLiteral("password")).toString();
        entry.url = o.value(QStringLiteral("url")).toString();
        entry.email = o.value(QStringLiteral("email")).toString();
        entry.notes = o.value(QStringLiteral("notes")).toString();
        entry.category = o.value(QStringLiteral("category")).toString();
        entry.favorite = o.value(QStringLiteral("favorite")).toBool();
        for (const QJsonValue &tag : o.value(QStringLiteral("tags")).toArray())
            entry.tags << tag.toString();
        const QJsonObject custom = o.value(QStringLiteral("customFields")).toObject();
        for (auto it = custom.constBegin(); it != custom.constEnd(); ++it)
            entry.customFields.insert(it.key(), it.value().toString());
        for (const QJsonValue &historyValue : o.value(QStringLiteral("history")).toArray()) {
            const QJsonObject h = historyValue.toObject();
            PasswordHistoryItem item;
            item.password = h.value(QStringLiteral("password")).toString();
            item.changedAt = parseDate(h.value(QStringLiteral("changedAt")));
            if (!item.password.isEmpty())
                entry.history.append(item);
        }
        entry.created = parseDate(o.value(QStringLiteral("created")));
        entry.modified = parseDate(o.value(QStringLiteral("modified")));
        result.entries.append(entry);
    }

    for (const QJsonValue &value : root.value(QStringLiteral("archivePasswords")).toArray()) {
        const QJsonObject o = value.toObject();
        ArchivePasswordRecord record;
        record.id = o.value(QStringLiteral("id")).toString();
        if (record.id.isEmpty())
            record.id = newId();
        record.archiveName = o.value(QStringLiteral("archiveName")).toString();
        record.archivePath = o.value(QStringLiteral("archivePath")).toString();
        record.password = o.value(QStringLiteral("password")).toString();
        record.group = o.value(QStringLiteral("group")).toString();
        record.note = o.value(QStringLiteral("note")).toString();
        record.created = parseDate(o.value(QStringLiteral("created")));
        record.modified = parseDate(o.value(QStringLiteral("modified")));
        result.archivePasswords.append(record);
    }

    if (decoded)
        *decoded = result;
    return true;
}

bool PasswordVault::writeTo(const QString &destination, QString *error) const
{
    if (!m_unlocked || m_key.size() != 32 || m_salt.size() != 16) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }

    const QByteArray plaintext = serializeJson();
    const QByteArray nonce = VaultCrypto::randomBytes(kNonceSize);
    if (nonce.size() != kNonceSize) {
        if (error)
            *error = QStringLiteral("secure random generator unavailable");
        return false;
    }

    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.writeRawData(kMagic, 8);
    stream << m_kdf.memoryKiB << m_kdf.iterations << m_kdf.lanes << m_kdf.version;
    stream << static_cast<quint32>(m_salt.size());
    stream.writeRawData(m_salt.constData(), m_salt.size());
    stream << static_cast<quint32>(nonce.size());
    stream.writeRawData(nonce.constData(), nonce.size());

    QByteArray ciphertext;
    QByteArray tag;
    if (!VaultCrypto::encrypt(m_key, nonce, header, plaintext, &ciphertext, &tag, error))
        return false;

    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("cannot write vault: %1").arg(file.errorString());
        return false;
    }
    // Restrict the temporary file before any ciphertext is written; the mode
    // travels with the atomic rename. The chmod below is a belt-and-braces
    // check for filesystems that ignore setPermissions on the temp file.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(header);
    file.write(ciphertext);
    file.write(tag);
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("cannot commit vault: %1").arg(file.errorString());
        return false;
    }
    ::chmod(QFile::encodeName(destination).constData(), 0600);
    return true;
}

bool PasswordVault::readFrom(const QString &source, const QString &masterPassword,
                             Decoded *decoded, QString *error, QByteArray *keyOut,
                             QByteArray *saltOut, KdfParams *kdfOut) const
{
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open vault: %1").arg(file.errorString());
        return false;
    }
    if (file.size() <= 0 || file.size() > kMaxVaultFileSize) {
        if (error)
            *error = QStringLiteral("vault file has an invalid size");
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.size() < 8 + 16 + kNonceSize + kTagSize
        || qstrncmp(data.constData(), kMagic, 8) != 0) {
        if (error)
            *error = QStringLiteral("file is not an E.I.S vault");
        return false;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    char magic[8];
    stream.readRawData(magic, 8);
    KdfParams kdf;
    quint32 saltSize = 0;
    quint32 nonceSize = 0;
    stream >> kdf.memoryKiB >> kdf.iterations >> kdf.lanes >> kdf.version;
    stream >> saltSize;
    if (saltSize < 16 || saltSize > 64 || stream.status() != QDataStream::Ok) {
        if (error)
            *error = QStringLiteral("vault header is corrupt");
        return false;
    }
    QByteArray salt(static_cast<int>(saltSize), Qt::Uninitialized);
    stream.readRawData(salt.data(), static_cast<int>(saltSize));
    stream >> nonceSize;
    if (nonceSize < 12 || nonceSize > 32 || stream.status() != QDataStream::Ok) {
        if (error)
            *error = QStringLiteral("vault header is corrupt");
        return false;
    }
    QByteArray nonce(static_cast<int>(nonceSize), Qt::Uninitialized);
    stream.readRawData(nonce.data(), static_cast<int>(nonceSize));
    if (stream.status() != QDataStream::Ok) {
        if (error)
            *error = QStringLiteral("vault header is corrupt");
        return false;
    }

    if (kdf.memoryKiB < 8192 || kdf.memoryKiB > 4 * 1024 * 1024
        || kdf.iterations < 1 || kdf.iterations > 100
        || kdf.lanes < 1 || kdf.lanes > 64
        || (kdf.version != 0x10 && kdf.version != 0x13)) {
        if (error)
            *error = QStringLiteral("vault KDF parameters are not acceptable");
        return false;
    }

    const qint64 headerLength = stream.device()->pos();
    if (headerLength <= 0 || data.size() < headerLength + kTagSize) {
        if (error)
            *error = QStringLiteral("vault payload is incomplete");
        return false;
    }
    const QByteArray aad = data.left(static_cast<int>(headerLength));
    const QByteArray ciphertext = data.mid(static_cast<int>(headerLength),
                                           data.size() - static_cast<int>(headerLength) - kTagSize);
    const QByteArray tag = data.right(kTagSize);

    QByteArray key;
    if (!VaultCrypto::deriveKey(masterPassword.toUtf8(), salt, kdf, &key, error))
        return false;
    QByteArray plaintext;
    if (!VaultCrypto::decrypt(key, nonce, aad, ciphertext, tag, &plaintext, error)) {
        VaultCrypto::cleanse(&key);
        return false;
    }

    const bool ok = deserializeJson(plaintext, decoded, error);
    VaultCrypto::cleanse(&plaintext);
    if (!ok) {
        VaultCrypto::cleanse(&key);
        return false;
    }

    if (keyOut)
        *keyOut = key;
    else
        VaultCrypto::cleanse(&key);
    if (saltOut)
        *saltOut = salt;
    if (kdfOut)
        *kdfOut = kdf;
    return true;
}

bool PasswordVault::addEntry(VaultEntry entry, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    if (entry.title.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("entry title is required");
        return false;
    }
    if (entry.id.isEmpty())
        entry.id = newId();
    for (const VaultEntry &existing : m_entries) {
        if (existing.id == entry.id) {
            if (error)
                *error = QStringLiteral("duplicate entry id");
            return false;
        }
    }
    if (!isValidEntryType(entry.type))
        entry.type = QStringLiteral("custom");
    entry.created = QDateTime::currentDateTime();
    entry.modified = entry.created;
    m_entries.append(entry);
    emit changed();
    return true;
}

bool PasswordVault::updateEntry(const VaultEntry &entry, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id != entry.id)
            continue;
        VaultEntry updated = entry;
        updated.created = m_entries[i].created.isValid() ? m_entries[i].created
                                                         : QDateTime::currentDateTime();
        updated.modified = QDateTime::currentDateTime();
        updated.history = m_entries[i].history;
        if (!m_entries[i].password.isEmpty() && m_entries[i].password != entry.password) {
            PasswordHistoryItem item;
            item.password = m_entries[i].password;
            item.changedAt = QDateTime::currentDateTime();
            updated.history.prepend(item);
            while (updated.history.size() > 10)
                updated.history.removeLast();
        }
        if (!isValidEntryType(updated.type))
            updated.type = QStringLiteral("custom");
        m_entries[i] = updated;
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("entry not found");
    return false;
}

bool PasswordVault::removeEntry(const QString &id, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id != id)
            continue;
        wipe(&m_entries[i].password);
        wipe(&m_entries[i].notes);
        for (auto it = m_entries[i].customFields.begin();
             it != m_entries[i].customFields.end(); ++it) {
            wipe(&it.value());
        }
        m_entries.removeAt(i);
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("entry not found");
    return false;
}

bool PasswordVault::addArchivePassword(ArchivePasswordRecord record, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    if (record.password.isEmpty()) {
        if (error)
            *error = QStringLiteral("password is empty");
        return false;
    }
    if (record.id.isEmpty())
        record.id = newId();
    record.archiveName = record.archiveName.isEmpty()
                             ? QFileInfo(record.archivePath).fileName()
                             : record.archiveName;
    record.created = QDateTime::currentDateTime();
    record.modified = record.created;
    m_archivePasswords.append(record);
    emit changed();
    return true;
}

bool PasswordVault::updateArchivePassword(const ArchivePasswordRecord &record, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    for (int i = 0; i < m_archivePasswords.size(); ++i) {
        if (m_archivePasswords[i].id != record.id)
            continue;
        ArchivePasswordRecord updated = record;
        updated.created = m_archivePasswords[i].created.isValid()
                              ? m_archivePasswords[i].created
                              : QDateTime::currentDateTime();
        updated.modified = QDateTime::currentDateTime();
        if (updated.archiveName.isEmpty())
            updated.archiveName = QFileInfo(updated.archivePath).fileName();
        m_archivePasswords[i] = updated;
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("archive password record not found");
    return false;
}

bool PasswordVault::removeArchivePassword(const QString &id, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    for (int i = 0; i < m_archivePasswords.size(); ++i) {
        if (m_archivePasswords[i].id != id)
            continue;
        wipe(&m_archivePasswords[i].password);
        m_archivePasswords.removeAt(i);
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("archive password record not found");
    return false;
}

QVector<VaultEntry> PasswordVault::searchEntries(const QString &query, const QString &category,
                                                 bool favoritesOnly) const
{
    const QString needle = query.trimmed().toLower();
    QVector<VaultEntry> result;
    for (const VaultEntry &entry : m_entries) {
        if (favoritesOnly && !entry.favorite)
            continue;
        if (!category.isEmpty() && entry.category.compare(category, Qt::CaseInsensitive) != 0)
            continue;
        if (!needle.isEmpty()) {
            const QString haystack = QStringList{entry.title, entry.username, entry.url, entry.email,
                                                 entry.category, entry.tags.join(QLatin1Char(' ')),
                                                 entry.notes}
                                         .join(QLatin1Char(' '))
                                         .toLower();
            if (!haystack.contains(needle))
                continue;
        }
        result.append(entry);
    }
    return result;
}

QStringList PasswordVault::categories() const
{
    QStringList result;
    for (const VaultEntry &entry : m_entries) {
        if (!entry.category.trimmed().isEmpty()
            && !result.contains(entry.category, Qt::CaseInsensitive))
            result << entry.category;
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QVector<ArchivePasswordRecord> PasswordVault::passwordsForArchive(const QString &archivePath) const
{
    QVector<ArchivePasswordRecord> exact;
    QVector<ArchivePasswordRecord> byName;
    const QString absolute = QFileInfo(archivePath).absoluteFilePath();
    const QString name = QFileInfo(archivePath).fileName();

    for (const ArchivePasswordRecord &record : m_archivePasswords) {
        if (!record.archivePath.isEmpty()
            && QFileInfo(record.archivePath).absoluteFilePath() == absolute) {
            exact.append(record);
        } else if (record.archiveName.compare(name, Qt::CaseInsensitive) == 0) {
            byName.append(record);
        }
    }
    return exact + byName;
}

bool PasswordVault::exportEncrypted(const QString &destination, QString *error) const
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("vault is locked");
        return false;
    }
    if (QFileInfo::exists(destination)) {
        if (error)
            *error = QStringLiteral("backup target already exists: %1").arg(destination);
        return false;
    }
    return writeTo(destination, error);
}

bool PasswordVault::importEncrypted(const QString &source, const QString &sourceMasterPassword,
                                    int *importedEntries, QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("unlock the destination vault first");
        return false;
    }
    Decoded decoded;
    if (!readFrom(source, sourceMasterPassword, &decoded, error))
        return false;

    int imported = 0;
    for (VaultEntry entry : decoded.entries) {
        for (const VaultEntry &existing : m_entries) {
            if (existing.id == entry.id) {
                entry.id = newId();
                break;
            }
        }
        m_entries.append(entry);
        ++imported;
    }
    for (ArchivePasswordRecord record : decoded.archivePasswords) {
        for (const ArchivePasswordRecord &existing : m_archivePasswords) {
            if (existing.id == record.id) {
                record.id = newId();
                break;
            }
        }
        m_archivePasswords.append(record);
    }

    if (importedEntries)
        *importedEntries = imported;
    if (!save(error)) {
        // Keep the imported data in memory so the caller can retry saving.
        return false;
    }
    emit changed();
    return true;
}

bool PasswordVault::restoreEncrypted(const QString &source, const QString &sourceMasterPassword,
                                     QString *error)
{
    if (!m_unlocked) {
        if (error)
            *error = QStringLiteral("unlock the destination vault first");
        return false;
    }
    Decoded decoded;
    if (!readFrom(source, sourceMasterPassword, &decoded, error))
        return false;

    for (VaultEntry &entry : m_entries) {
        wipe(&entry.password);
        for (auto it = entry.customFields.begin(); it != entry.customFields.end(); ++it)
            wipe(&it.value());
    }
    m_entries = decoded.entries;
    m_archivePasswords = decoded.archivePasswords;
    if (!save(error))
        return false;
    emit changed();
    return true;
}

void PasswordVault::wipeState()
{
    for (VaultEntry &entry : m_entries) {
        wipe(&entry.password);
        wipe(&entry.notes);
        for (auto it = entry.customFields.begin(); it != entry.customFields.end(); ++it)
            wipe(&it.value());
        for (PasswordHistoryItem &item : entry.history)
            wipe(&item.password);
    }
    for (ArchivePasswordRecord &record : m_archivePasswords)
        wipe(&record.password);

    m_entries.clear();
    m_archivePasswords.clear();
    VaultCrypto::cleanse(&m_key);
    VaultCrypto::cleanse(&m_salt);
    m_unlocked = false;
}

} // namespace eis
