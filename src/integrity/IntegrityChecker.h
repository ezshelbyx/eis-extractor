#pragma once

#include "archive/ArchiveService.h"
#include "core/Types.h"

namespace eis {

class IntegrityChecker {
public:
    IntegrityChecker() = default;
    IntegrityResult check(const QString &archivePath, const QString &password = QString(),
                         const QString &expectedSha256 = QString());
};

} // namespace eis
