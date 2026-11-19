#include "verification/HashEngine.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>

#include <openssl/evp.h>

#include <memory>

namespace eis {

namespace {

const EVP_MD *mdFor(HashAlgorithm algorithm)
{
    switch (algorithm) {
    case HashAlgorithm::Sha256:
        return EVP_sha256();
    case HashAlgorithm::Sha512:
        return EVP_sha512();
    case HashAlgorithm::Sha1:
        return EVP_sha1();
    case HashAlgorithm::Md5:
        return EVP_md5();
    }
    return EVP_sha256();
}

QString digestLengthToAlgorithm(int length)
{
    switch (length) {
    case 32:
        return QStringLiteral("MD5");
    case 40:
        return QStringLiteral("SHA-1");
    case 64:
        return QStringLiteral("SHA-256");
    case 128:
        return QStringLiteral("SHA-512");
    default:
        return QString();
    }
}

} // namespace

QString hashAlgorithmName(HashAlgorithm algorithm)
{
    switch (algorithm) {
    case HashAlgorithm::Sha256:
        return QStringLiteral("SHA-256");
    case HashAlgorithm::Sha512:
        return QStringLiteral("SHA-512");
    case HashAlgorithm::Sha1:
        return QStringLiteral("SHA-1");
    case HashAlgorithm::Md5:
        return QStringLiteral("MD5");
    }
    return QStringLiteral("SHA-256");
}

QStringList hashAlgorithmNames()
{
    return {QStringLiteral("SHA-256"), QStringLiteral("SHA-512"),
            QStringLiteral("SHA-1"), QStringLiteral("MD5")};
}

HashAlgorithm hashAlgorithmFromName(const QString &name, bool *ok)
{
    const QString lower = name.trimmed().toLower().remove(QLatin1Char('-'));
    if (ok)
        *ok = true;
    if (lower == QLatin1String("sha512"))
        return HashAlgorithm::Sha512;
    if (lower == QLatin1String("sha1"))
        return HashAlgorithm::Sha1;
    if (lower == QLatin1String("md5"))
        return HashAlgorithm::Md5;
    if (lower == QLatin1String("sha256"))
        return HashAlgorithm::Sha256;
    if (ok)
        *ok = false;
    return HashAlgorithm::Sha256;
}

HashResult HashEngine::hashFile(const QString &path, HashAlgorithm algorithm,
                                const std::atomic<bool> *cancel,
                                const ProgressCallback &progress)
{
    HashResult result;
    result.path = path;
    result.algorithm = hashAlgorithmName(algorithm);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("cannot open: %1").arg(file.errorString());
        return result;
    }
    const qint64 total = file.size();

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        result.error = QStringLiteral("cannot allocate digest context");
        return result;
    }
    const EVP_MD *md = mdFor(algorithm);
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        result.error = QStringLiteral("cannot initialize digest");
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    qint64 done = 0;
    while (!file.atEnd()) {
        if (cancel && cancel->load()) {
            result.cancelled = true;
            result.error = QStringLiteral("cancelled");
            EVP_MD_CTX_free(ctx);
            return result;
        }
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read < 0) {
            result.error = QStringLiteral("read error: %1").arg(file.errorString());
            EVP_MD_CTX_free(ctx);
            return result;
        }
        if (read == 0)
            break;
        if (EVP_DigestUpdate(ctx, buffer.constData(), static_cast<size_t>(read)) != 1) {
            result.error = QStringLiteral("digest update failed");
            EVP_MD_CTX_free(ctx);
            return result;
        }
        done += read;
        if (progress)
            progress(done, total);
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digestLength) != 1) {
        EVP_MD_CTX_free(ctx);
        result.error = QStringLiteral("digest finalization failed");
        return result;
    }
    EVP_MD_CTX_free(ctx);

    result.digest = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(digest), static_cast<int>(digestLength)).toHex());
    result.bytesRead = done;
    result.durationMs = timer.elapsed();
    result.ok = true;
    return result;
}

bool HashEngine::hashFileAll(const QString &path, QVector<HashResult> *results,
                             const std::atomic<bool> *cancel, QString *error)
{
    if (results)
        results->clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open: %1").arg(file.errorString());
        return false;
    }

    struct Ctx {
        HashAlgorithm algorithm;
        EVP_MD_CTX *ctx = nullptr;
    };
    const HashAlgorithm algorithms[] = {HashAlgorithm::Sha256, HashAlgorithm::Sha512,
                                        HashAlgorithm::Sha1, HashAlgorithm::Md5};

    QVector<Ctx> contexts;
    for (HashAlgorithm algorithm : algorithms) {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx || EVP_DigestInit_ex(ctx, mdFor(algorithm), nullptr) != 1) {
            if (ctx)
                EVP_MD_CTX_free(ctx);
            for (const Ctx &c : contexts)
                EVP_MD_CTX_free(c.ctx);
            if (error)
                *error = QStringLiteral("cannot initialize digest contexts");
            return false;
        }
        contexts.append({algorithm, ctx});
    }

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    qint64 done = 0;
    while (!file.atEnd()) {
        if (cancel && cancel->load()) {
            for (const Ctx &c : contexts)
                EVP_MD_CTX_free(c.ctx);
            if (error)
                *error = QStringLiteral("cancelled");
            return false;
        }
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read <= 0)
            break;
        for (const Ctx &c : contexts) {
            if (EVP_DigestUpdate(c.ctx, buffer.constData(), static_cast<size_t>(read)) != 1) {
                for (const Ctx &other : contexts)
                    EVP_MD_CTX_free(other.ctx);
                if (error)
                    *error = QStringLiteral("digest update failed");
                return false;
            }
        }
        done += read;
    }

    for (const Ctx &c : contexts) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        if (EVP_DigestFinal_ex(c.ctx, digest, &length) == 1) {
            HashResult r;
            r.ok = true;
            r.path = path;
            r.algorithm = hashAlgorithmName(c.algorithm);
            r.digest = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(digest),
                                                      static_cast<int>(length)).toHex());
            r.bytesRead = done;
            if (results)
                results->append(r);
        }
        EVP_MD_CTX_free(c.ctx);
    }
    return results && results->size() == 4;
}

bool HashEngine::generateChecksumFile(const QStringList &files, const QString &outputPath,
                                      HashAlgorithm algorithm, QString *error,
                                      const std::atomic<bool> *cancel)
{
    QSaveFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(outputPath, out.errorString());
        return false;
    }

    for (const QString &path : files) {
        const HashResult result = hashFile(path, algorithm, cancel);
        if (!result.ok) {
            out.cancelWriting();
            if (error)
                *error = result.error.isEmpty()
                             ? QStringLiteral("hashing failed: %1").arg(path)
                             : result.error;
            return false;
        }
        const QString name = QFileInfo(path).fileName();
        out.write(result.digest.toLatin1());
        out.write("  ");
        out.write(name.toUtf8());
        out.write("\n");
    }
    if (!out.commit()) {
        if (error)
            *error = QStringLiteral("cannot commit %1: %2").arg(outputPath, out.errorString());
        return false;
    }
    return true;
}

QVector<ChecksumEntry> HashEngine::readChecksumFile(const QString &checksumFile, QString *error)
{
    QVector<ChecksumEntry> entries;
    QFile file(checksumFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot open checksum file: %1").arg(file.errorString());
        return entries;
    }

    static const QRegularExpression bsd(
        QStringLiteral("^\\s*(SHA256|SHA512|SHA1|MD5)\\s*\\((.*)\\)\\s*=\\s*([0-9A-Fa-f]+)\\s*$"));
    static const QRegularExpression gnu(
        QStringLiteral("^\\s*([0-9A-Fa-f]{32,128})[\\s\\*]+(.+?)\\s*$"));

    int lineNumber = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        ++lineNumber;
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        QRegularExpressionMatch match = bsd.match(line);
        if (match.hasMatch()) {
            ChecksumEntry e;
            e.expected = match.captured(3).toLower();
            e.path = match.captured(2);
            e.line = lineNumber;
            if (e.path.isEmpty() || e.expected.isEmpty())
                continue;
            entries.append(e);
            continue;
        }

        match = gnu.match(line);
        if (match.hasMatch()) {
            ChecksumEntry e;
            e.expected = match.captured(1).toLower();
            e.path = match.captured(2);
            e.line = lineNumber;
            if (e.path.isEmpty() || e.expected.isEmpty())
                continue;
            entries.append(e);
        }
    }

    if (entries.isEmpty() && error)
        *error = QStringLiteral("no checksum entries found");
    return entries;
}

ChecksumFileResult HashEngine::verifyChecksumFile(const QString &checksumFile,
                                                  const std::atomic<bool> *cancel)
{
    ChecksumFileResult result;
    result.checksumFile = checksumFile;

    QString parseError;
    const QVector<ChecksumEntry> entries = readChecksumFile(checksumFile, &parseError);
    if (entries.isEmpty()) {
        result.errors << (parseError.isEmpty() ? QStringLiteral("empty checksum file") : parseError);
        return result;
    }

    const QString baseDir = QFileInfo(checksumFile).absolutePath();
    QString algorithm;
    bool mixed = false;
    for (const ChecksumEntry &entry : entries) {
        if (cancel && cancel->load()) {
            result.errors << QStringLiteral("cancelled");
            return result;
        }

        const QString name = digestLengthToAlgorithm(entry.expected.size());
        if (name.isEmpty()) {
            result.errors << QStringLiteral("line %1: unsupported digest length").arg(entry.line);
            ++result.mismatched;
            ++result.checked;
            continue;
        }
        if (algorithm.isEmpty())
            algorithm = name;
        else if (algorithm != name)
            mixed = true;

        QString path = entry.path;
        if (QDir::isRelativePath(path))
            path = baseDir + QLatin1Char('/') + path;

        if (!QFileInfo::exists(path)) {
            result.errors << QStringLiteral("missing: %1").arg(entry.path);
            ++result.missing;
            ++result.checked;
            continue;
        }

        bool ok = false;
        const HashAlgorithm algo = hashAlgorithmFromName(name, &ok);
        const HashResult hash = hashFile(path, algo, cancel);
        ++result.checked;
        if (!hash.ok) {
            result.errors << QStringLiteral("%1: %2").arg(entry.path, hash.error);
            ++result.mismatched;
            continue;
        }
        if (hash.digest.compare(entry.expected, Qt::CaseInsensitive) == 0) {
            ++result.matched;
        } else {
            ++result.mismatched;
            result.mismatches << QStringLiteral("%1\n  expected %2\n  actual   %3")
                                     .arg(entry.path, entry.expected, hash.digest);
        }
    }

    result.algorithm = mixed ? QStringLiteral("mixed") : algorithm;
    result.ok = result.mismatched == 0 && result.missing == 0 && result.errors.isEmpty();
    return result;
}

} // namespace eis
