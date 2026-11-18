#pragma once

#include "backends/Backend.h"

namespace eis {

class SevenZipBackend final : public ArchiveBackend {
public:
    QString name() const override { return QStringLiteral("7z"); }
    BackendKind kind() const override { return BackendKind::SevenZip; }
    bool available() const override;
    QStringList formats() const override;
    QString version() const override;
    QString executablePath() const override;

    bool probe(const QString &archivePath,
               const QString &password,
               QString *formatOut,
               QString *errorOut) override;

    ArchiveListing list(const QString &archivePath,
                        const QString &password,
                        int maxEntries,
                        const std::atomic<bool> *cancel,
                        const std::function<void(int)> &progress) override;

    ExtractionResult extract(const ExtractionOptions &options,
                             JobControl &control,
                             const ProgressCallback &progress) override;
};

} // namespace eis
