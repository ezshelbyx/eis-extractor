#pragma once

#include "core/Types.h"

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace eis {

enum class CompressionLevel { Store, Fast, Normal, Maximum };

QString compressionLevelName(CompressionLevel level);

struct CompressionRequest {
    QStringList sources;        // files and/or directories
    QString outputPath;         // archive to create
    QString format;             // canonical id from CapabilityRegistry
    CompressionLevel level = CompressionLevel::Normal;
    QString password;           // empty = no encryption
    bool encryptHeaders = true; // 7z header encryption; ignored by other formats
    qint64 volumeSize = 0;      // bytes per volume, 0 = single file
    bool overwrite = false;
};

struct CompressionResult {
    bool ok = false;
    bool cancelled = false;
    QString outputPath;
    QString backend;
    QString error;
    QStringList warnings;
    qint64 inputBytes = 0;
    qint64 outputBytes = 0;
    qint64 durationMs = 0;
    int filesAdded = 0;
    bool encrypted = false;
};

// Creates archives only in formats whose backend is actually installed.
// Never fakes progress: percentages come from the backend, or the operation
// is reported as indeterminate.
class CompressionEngine {
public:
    using ProgressCallback = std::function<void(const ProgressInfo &)>;

    // Validates a request without touching disk output.
    static bool validate(const CompressionRequest &request, QString *error);

    static CompressionResult create(const CompressionRequest &request,
                                    JobControl *control,
                                    const ProgressCallback &progress = nullptr);

    // Builds a sensible default output name for sources and format.
    static QString suggestOutputPath(const QStringList &sources, const QString &format);
};

} // namespace eis

Q_DECLARE_METATYPE(eis::CompressionResult)
