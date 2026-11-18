#include "security/PathGuard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace eis {

namespace {

bool isDriveLetter(const QString &path)
{
    return path.size() >= 2 && path.at(1) == QLatin1Char(':') && path.at(0).isLetter();
}

bool hasControlChars(const QString &path)
{
    for (const QChar ch : path) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return true;
    }
    return false;
}

QString lstatError(const QString &path, int err)
{
    return QStringLiteral("%1: %2").arg(path, QString::fromLocal8Bit(std::strerror(err)));
}

} // namespace

QString PathGuard::normalizeEntryPath(const QString &raw, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };

    if (raw.isEmpty())
        return fail(QStringLiteral("empty entry path"));
    if (raw.contains(QChar(QChar::Null)))
        return fail(QStringLiteral("entry path contains NUL"));
    if (hasControlChars(raw))
        return fail(QStringLiteral("entry path contains control characters"));

    QString path = raw;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (path.startsWith(QLatin1Char('/')))
        return fail(QStringLiteral("absolute entry path rejected: %1").arg(raw));
    if (isDriveLetter(path))
        return fail(QStringLiteral("drive-letter entry path rejected: %1").arg(raw));

    QStringList parts;
    const QStringList segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        if (segment == QLatin1String("."))
            continue;
        if (segment == QLatin1String(".."))
            return fail(QStringLiteral("parent traversal rejected: %1").arg(raw));
        if (segment.size() > 255)
            return fail(QStringLiteral("entry path component too long: %1").arg(raw));
        parts << segment;
    }

    if (parts.isEmpty())
        return fail(QStringLiteral("entry path normalizes to nothing: %1").arg(raw));

    const QString normalized = parts.join(QLatin1Char('/'));
    if (normalized.size() > 4096)
        return fail(QStringLiteral("entry path too long: %1").arg(raw));
    return normalized;
}

QString PathGuard::safeJoin(const QString &destRoot, const QString &relativePath)
{
    if (relativePath.isEmpty())
        return destRoot;
    return destRoot + QLatin1Char('/') + relativePath;
}

bool PathGuard::ensureSafeDirectory(const QString &destRoot, const QString &relativeDir, QString *error)
{
    if (relativeDir.isEmpty())
        return true;

    QString current = destRoot;
    const QStringList parts = relativeDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        current += QLatin1Char('/') + part;

        struct stat st {};
        const QByteArray encoded = QFile::encodeName(current);
        if (::lstat(encoded.constData(), &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                if (error)
                    *error = QStringLiteral("refusing to traverse symlink: %1").arg(current);
                return false;
            }
            if (!S_ISDIR(st.st_mode)) {
                if (error)
                    *error = QStringLiteral("path component is not a directory: %1").arg(current);
                return false;
            }
            continue;
        }

        if (errno != ENOENT) {
            if (error)
                *error = lstatError(current, errno);
            return false;
        }

        if (::mkdir(encoded.constData(), 0755) != 0 && errno != EEXIST) {
            if (error)
                *error = QStringLiteral("cannot create directory %1: %2")
                             .arg(current, QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
    }
    return true;
}

PathGuard::LinkCheck PathGuard::validateLinkTarget(const QString &entryRelativePath,
                                                   const QString &target,
                                                   bool targetIsArchiveRelative)
{
    LinkCheck result;

    if (target.isEmpty()) {
        result.error = QStringLiteral("empty link target");
        return result;
    }
    if (target.contains(QChar(QChar::Null))) {
        result.error = QStringLiteral("link target contains NUL");
        return result;
    }

    QString normalizedTarget = target;
    normalizedTarget.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalizedTarget.startsWith(QLatin1Char('/')) || isDriveLetter(normalizedTarget)) {
        result.error = QStringLiteral("absolute link target rejected: %1").arg(target);
        return result;
    }

    QStringList stack;
    if (!targetIsArchiveRelative) {
        const int slash = entryRelativePath.lastIndexOf(QLatin1Char('/'));
        if (slash > 0) {
            const QStringList parent = entryRelativePath.left(slash).split(QLatin1Char('/'), Qt::SkipEmptyParts);
            for (const QString &part : parent)
                stack << part;
        }
    }

    const QStringList targetParts = normalizedTarget.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : targetParts) {
        if (part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (stack.isEmpty()) {
                result.error = QStringLiteral("link target escapes destination: %1").arg(target);
                return result;
            }
            stack.removeLast();
            continue;
        }
        stack << part;
    }

    if (stack.isEmpty()) {
        result.error = QStringLiteral("link target resolves to destination root: %1").arg(target);
        return result;
    }

    result.ok = true;
    result.resolved = stack.join(QLatin1Char('/'));
    return result;
}

bool PathGuard::pathExistsNoFollow(const QString &absolutePath)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(absolutePath).constData(), &st) == 0;
}

QString PathGuard::uniqueTargetPath(const QString &absolutePath,
                                    CollisionPolicy policy,
                                    bool *skip,
                                    bool *renamed)
{
    if (skip)
        *skip = false;
    if (renamed)
        *renamed = false;

    if (!pathExistsNoFollow(absolutePath))
        return absolutePath;

    if (policy == CollisionPolicy::Skip) {
        if (skip)
            *skip = true;
        return absolutePath;
    }

    if (policy == CollisionPolicy::Overwrite) {
        struct stat st {};
        if (::lstat(QFile::encodeName(absolutePath).constData(), &st) == 0
            && S_ISREG(st.st_mode)) {
            return absolutePath;
        }
        // Directories and symlinks are never overwritten; fall through to a
        // unique name instead.
    }

    const int slash = absolutePath.lastIndexOf(QLatin1Char('/'));
    const QString directory = slash >= 0 ? absolutePath.left(slash + 1) : QString();
    const QString fileName = slash >= 0 ? absolutePath.mid(slash + 1) : absolutePath;

    int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        dot = -1;
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    const QString suffix = dot > 0 ? fileName.mid(dot) : QString();

    for (int i = 1; i < 100000; ++i) {
        const QString candidate = directory + stem + QStringLiteral(" (%1)").arg(i) + suffix;
        if (!pathExistsNoFollow(candidate)) {
            if (renamed)
                *renamed = true;
            return candidate;
        }
    }

    if (skip)
        *skip = true;
    return absolutePath;
}

bool PathGuard::isInside(const QString &rootCanonical, const QString &candidate)
{
    const QString root = QDir::cleanPath(rootCanonical);
    const QString path = QDir::cleanPath(candidate);
    if (root == QLatin1String("/"))
        return path.startsWith(QLatin1Char('/'));
    return path == root || path.startsWith(root + QLatin1Char('/'));
}

QString PathGuard::canonicalDestination(const QString &requested, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };

    if (requested.trimmed().isEmpty())
        return fail(QStringLiteral("destination is empty"));

    const QFileInfo info(requested);
    if (info.exists() && !info.isDir())
        return fail(QStringLiteral("destination exists and is not a directory: %1").arg(requested));

    if (!QDir().mkpath(requested))
        return fail(QStringLiteral("cannot create destination: %1").arg(requested));

    const QString canonical = QFileInfo(requested).canonicalFilePath();
    if (canonical.isEmpty())
        return fail(QStringLiteral("cannot resolve destination: %1").arg(requested));
    if (canonical == QLatin1String("/"))
        return fail(QStringLiteral("refusing to extract directly into the filesystem root"));
    if (!QFileInfo(canonical).isWritable())
        return fail(QStringLiteral("destination is not writable: %1").arg(canonical));
    return canonical;
}

} // namespace eis
