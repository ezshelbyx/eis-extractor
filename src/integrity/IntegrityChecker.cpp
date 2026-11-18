#include "integrity/IntegrityChecker.h"

#include "archive/ArchiveService.h"

namespace eis {

IntegrityResult IntegrityChecker::check(const QString &archivePath, const QString &password,
                                       const QString &expectedSha256)
{
    ArchiveService service;
    IntegrityResult result = service.test(archivePath, password);
    if (!expectedSha256.isEmpty() && !result.sha256.isEmpty()) {
        result.expectedMatch = (result.sha256.compare(expectedSha256, Qt::CaseInsensitive) == 0) ? 1 : 0;
        if (result.expectedMatch == 0)
            result.ok = false;
            if (result.error.isEmpty())
                result.error = QStringLiteral("SHA-256 mismatch");
    } else {
        result.expectedMatch = -1;
    }
    return result;
}

} // namespace eis
