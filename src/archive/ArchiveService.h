#pragma once

#include "archive/ArchiveTypes.h"
#include "backends/Backend.h"
#include "backends/BackendRegistry.h"

#include <QString>
#include <memory>

namespace eis {

// High-level facade used by the UI and tests. Owns a backend registry and
// keeps listing strictly read-only (no extraction).
class ArchiveService {
public:
    ArchiveService();

    BackendRegistry &registry() { return m_registry; }
    const BackendRegistry &registry() const { return m_registry; }

    ArchiveListing list(const QString &archivePath, const QString &password = QString(),
                       int maxEntries = 100000, const std::atomic<bool> *cancel = nullptr);

    // Test integrity: list + (optionally) verify CRCs via libarchive/7z.
    IntegrityResult test(const QString &archivePath, const QString &password = QString());

    // SHA-256 of the archive file itself.
    QString sha256File(const QString &path) const;

    // Detect format without listing.
    QString detectFormat(const QString &archivePath, const QString &password = QString()) const;

private:
    BackendRegistry m_registry;
};

} // namespace eis
