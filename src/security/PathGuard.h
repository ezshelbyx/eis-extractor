#pragma once

#include "core/Types.h"

#include <QString>

namespace eis {

// All archive-controlled paths pass through this class before touching disk.
class PathGuard {
public:
    struct LinkCheck {
        bool ok = false;
        QString resolved; // destination-relative, lexically resolved
        QString error;
    };

    // Rejects absolute paths, drive letters, parent traversal and control
    // characters. Returns a normalized destination-relative path, or an empty
    // string with *error set.
    static QString normalizeEntryPath(const QString &raw, QString *error = nullptr);

    // Creates the directory chain under destRoot without ever following a
    // symlink component. Rejects existing symlinks and non-directories.
    static bool ensureSafeDirectory(const QString &destRoot, const QString &relativeDir, QString *error);

    static QString safeJoin(const QString &destRoot, const QString &relativePath);

    // Validates symlink/hardlink targets. Symlink targets are resolved relative
    // to the entry's parent; hardlink targets are archive-relative.
    static LinkCheck validateLinkTarget(const QString &entryRelativePath,
                                        const QString &target,
                                        bool targetIsArchiveRelative);

    static bool pathExistsNoFollow(const QString &absolutePath);

    // Applies the collision policy. *skip is set for "skip"; *renamed tells the
    // caller a new unique name was chosen.
    static QString uniqueTargetPath(const QString &absolutePath,
                                    CollisionPolicy policy,
                                    bool *skip,
                                    bool *renamed);

    static bool isInside(const QString &rootCanonical, const QString &candidate);

    // Creates (if needed) and canonicalizes the destination directory.
    static QString canonicalDestination(const QString &requested, QString *error);
};

} // namespace eis
