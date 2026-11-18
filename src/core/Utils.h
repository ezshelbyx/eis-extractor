#pragma once

#include <QString>
#include <QStringList>

namespace eis::utils {

QString humanBytes(qint64 bytes);
QString humanSpeed(double bytesPerSecond);
QString humanDuration(qint64 milliseconds);
QString percentString(qint64 done, qint64 total);

QString stripArchiveExtensions(const QString &fileName);
QString archiveBaseName(const QString &path);
QString uniqueDirectory(const QString &desired);
QStringList archiveExtensions();
QString archiveNameFilter();
bool looksLikeArchive(const QString &path);

} // namespace eis::utils
