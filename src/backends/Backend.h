#pragma once

#include "archive/ArchiveTypes.h"
#include "core/Types.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace eis {

using ProgressCallback = std::function<void(const ProgressInfo &)>;

// A backend reports capability only for operations it can actually perform.
// Callers always probe the concrete archive before trusting it.
class ArchiveBackend {
public:
    virtual ~ArchiveBackend() = default;

    virtual QString name() const = 0;
    virtual BackendKind kind() const = 0;
    virtual bool available() const = 0;
    virtual QStringList formats() const = 0;
    virtual QString version() const = 0;
    virtual QString executablePath() const { return QString(); }

    virtual bool probe(const QString &archivePath,
                       const QString &password,
                       QString *formatOut,
                       QString *errorOut) = 0;

    virtual ArchiveListing list(const QString &archivePath,
                                const QString &password,
                                int maxEntries,
                                const std::atomic<bool> *cancel,
                                const std::function<void(int)> &progress) = 0;

    virtual ExtractionResult extract(const ExtractionOptions &options,
                                     JobControl &control,
                                     const ProgressCallback &progress) = 0;
};

} // namespace eis
