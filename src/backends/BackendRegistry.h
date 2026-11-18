#pragma once

#include "backends/Backend.h"

#include <memory>
#include <vector>

namespace eis {

// Selects a backend for an archive. libarchive is preferred; 7-Zip is the
// fallback for formats libarchive cannot handle or when explicitly requested.
class BackendRegistry {
public:
    BackendRegistry();

    std::vector<std::shared_ptr<ArchiveBackend>> backends() const;
    std::shared_ptr<ArchiveBackend> primary() const;
    std::shared_ptr<ArchiveBackend> fallback() const;

    // Returns the first backend that probes successfully, trying libarchive
    // first. *formatOut receives the detected format.
    std::shared_ptr<ArchiveBackend> select(const QString &archivePath,
                                            const QString &password,
                                            QString *formatOut,
                                            QString *errorOut) const;

    // All formats advertised across available backends.
    QStringList supportedFormats() const;
    QString capabilityReport() const;

private:
    std::shared_ptr<ArchiveBackend> m_libarchive;
    std::shared_ptr<ArchiveBackend> m_sevenzip;
};

} // namespace eis
