#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

namespace eis {

enum class HashAlgorithm { Sha256, Sha512, Sha1, Md5 };

QString hashAlgorithmName(HashAlgorithm algorithm);
QStringList hashAlgorithmNames();
HashAlgorithm hashAlgorithmFromName(const QString &name, bool *ok = nullptr);

struct HashOptions {
    HashAlgorithm algorithm = HashAlgorithm::Sha256;
    bool allowLegacy = true; // SHA-1/MD5 are legacy-only; callers may refuse them
};

struct HashResult {
    bool ok = false;
    bool cancelled = false;
    QString path;
    QString algorithm;
    QString digest;
    qint64 bytesRead = 0;
    qint64 durationMs = 0;
    QString error;
};

struct ChecksumEntry {
    QString expected;
    QString path;
    qint64 line = 0;
};

struct ChecksumFileResult {
    bool ok = false;
    int checked = 0;
    int matched = 0;
    int mismatched = 0;
    int missing = 0;
    QStringList errors;
    QStringList mismatches;
    QString checksumFile;
    QString algorithm;
};

// Streaming hashing; never loads whole files into memory.
class HashEngine {
public:
    using ProgressCallback = std::function<void(qint64 bytesDone, qint64 bytesTotal)>;

    static HashResult hashFile(const QString &path,
                               HashAlgorithm algorithm,
                               const std::atomic<bool> *cancel = nullptr,
                               const ProgressCallback &progress = nullptr);

    // Reads the file once and returns every digest.
    static bool hashFileAll(const QString &path,
                            QVector<HashResult> *results,
                            const std::atomic<bool> *cancel = nullptr,
                            QString *error = nullptr);

    // Writes "HEX  filename" lines (GNU coreutils format).
    static bool generateChecksumFile(const QStringList &files,
                                     const QString &outputPath,
                                     HashAlgorithm algorithm,
                                     QString *error,
                                     const std::atomic<bool> *cancel = nullptr);

    static QVector<ChecksumEntry> readChecksumFile(const QString &checksumFile, QString *error);

    // Verifies entries against files. Relative names resolve against the
    // checksum file's directory.
    static ChecksumFileResult verifyChecksumFile(const QString &checksumFile,
                                                 const std::atomic<bool> *cancel = nullptr);
};

} // namespace eis
