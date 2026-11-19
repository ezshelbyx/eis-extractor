#include "core/Capabilities.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <archive.h>

namespace eis {

namespace {

// libarchive formats that can actually be written by this build.
bool libarchiveCanWrite(const char *name)
{
    archive *a = archive_write_new();
    if (!a)
        return false;
    const bool ok = archive_write_set_format_by_name(a, name) == ARCHIVE_OK;
    archive_write_free(a);
    return ok;
}

bool libarchiveCanFilter(const char *name)
{
    archive *a = archive_write_new();
    if (!a)
        return false;
    const bool ok = archive_write_add_filter_by_name(a, name) == ARCHIVE_OK;
    archive_write_free(a);
    return ok;
}

FormatCapability make(const QString &id, const QString &label, const QStringList &ext)
{
    FormatCapability c;
    c.id = id;
    c.label = label;
    c.extensions = ext;
    return c;
}

} // namespace

CapabilityRegistry &CapabilityRegistry::instance()
{
    static CapabilityRegistry registry;
    return registry;
}

CapabilityRegistry::CapabilityRegistry()
{
    const bool sevenZip = sevenZipAvailable();
    const QStringList szFormats = sevenZipFormatNames();
    auto szHas = [&szFormats](const QString &name) {
        return szFormats.contains(name, Qt::CaseInsensitive);
    };

    const bool libZip = libarchiveCanWrite("zip");
    // libarchive names its TAR writer "pax"/"ustar", not "tar".
    const bool libTar = libarchiveCanWrite("pax") || libarchiveCanWrite("ustar");
    const bool libCpio = libarchiveCanWrite("cpio");
    const bool libRaw = libarchiveCanWrite("raw");
    const bool gzip = libarchiveCanFilter("gzip");
    const bool bzip2 = libarchiveCanFilter("bzip2");
    const bool xz = libarchiveCanFilter("xz");
    const bool zstd = libarchiveCanFilter("zstd");
    const bool lz4 = libarchiveCanFilter("lz4");
    const bool lzma = libarchiveCanFilter("lzma");

    // ZIP
    FormatCapability zip = make(QStringLiteral("zip"), QStringLiteral("ZIP"),
                                {QStringLiteral("zip"), QStringLiteral("jar"),
                                 QStringLiteral("docx"), QStringLiteral("xlsx"),
                                 QStringLiteral("odt"), QStringLiteral("epub")});
    zip.canRead = true;
    zip.canCreate = libZip || (sevenZip && szHas(QStringLiteral("zip")));
    zip.canEncrypt = sevenZip && szHas(QStringLiteral("zip"));
    zip.canMultiVolume = sevenZip && szHas(QStringLiteral("zip"));
    zip.readBackend = QStringLiteral("libarchive");
    zip.writeBackend = zip.canEncrypt ? QStringLiteral("7-Zip (AES-256)")
                                      : QStringLiteral("libarchive");
    if (zip.canEncrypt)
        zip.note = QStringLiteral("Encrypted ZIP uses 7-Zip AES-256; not all ZIP tools read it.");
    m_formats.append(zip);

    // 7Z / RAR
    FormatCapability sevenz = make(QStringLiteral("7z"), QStringLiteral("7-Zip"),
                                   {QStringLiteral("7z")});
    sevenz.canRead = true;
    sevenz.canCreate = sevenZip && szHas(QStringLiteral("7z"));
    sevenz.canEncrypt = sevenz.canCreate;
    sevenz.canMultiVolume = sevenz.canCreate;
    sevenz.readBackend = QStringLiteral("libarchive + 7-Zip");
    sevenz.writeBackend = QStringLiteral("7-Zip");
    sevenz.note = sevenz.canEncrypt ? QStringLiteral("AES-256 encryption, optional encrypted headers.")
                                    : QStringLiteral("7-Zip is not installed; creation unavailable.");
    m_formats.append(sevenz);

    FormatCapability rar = make(QStringLiteral("rar"), QStringLiteral("RAR / RAR5"),
                                {QStringLiteral("rar")});
    rar.canRead = sevenZip && (szHas(QStringLiteral("rar")) || szHas(QStringLiteral("rar5")));
    rar.readBackend = QStringLiteral("7-Zip");
    rar.note = QStringLiteral("Read/extract only; RAR creation is not available.");
    m_formats.append(rar);

    // TAR variants
    struct TarVariant {
        const char *id;
        const char *label;
        QStringList ext;
        bool filterOk;
        const char *filter;
    };
    const TarVariant variants[] = {
        {"tar", "TAR", {QStringLiteral("tar")}, true, nullptr},
        {"tar.gz", "TAR.GZ", {QStringLiteral("tar.gz"), QStringLiteral("tgz")}, gzip, "gzip"},
        {"tar.bz2", "TAR.BZ2", {QStringLiteral("tar.bz2"), QStringLiteral("tbz2")}, bzip2, "bzip2"},
        {"tar.xz", "TAR.XZ", {QStringLiteral("tar.xz"), QStringLiteral("txz")}, xz, "xz"},
        {"tar.zst", "TAR.ZST", {QStringLiteral("tar.zst"), QStringLiteral("tzst")}, zstd, "zstd"},
        {"tar.lz4", "TAR.LZ4", {QStringLiteral("tar.lz4")}, lz4, "lz4"},
        {"tar.lzma", "TAR.LZMA", {QStringLiteral("tar.lzma")}, lzma, "lzma"},
    };
    for (const TarVariant &v : variants) {
        FormatCapability c = make(QString::fromLatin1(v.id), QString::fromLatin1(v.label), v.ext);
        c.canRead = true;
        c.canCreate = libTar && v.filterOk;
        c.readBackend = QStringLiteral("libarchive");
        c.writeBackend = QStringLiteral("libarchive");
        if (!c.canCreate)
            c.note = QStringLiteral("Compression filter not available in this libarchive build.");
        m_formats.append(c);
    }

    // Single-stream compressed formats
    struct StreamVariant {
        const char *id;
        const char *label;
        QStringList ext;
        bool filterOk;
    };
    const StreamVariant streams[] = {
        {"gz", "GZIP", {QStringLiteral("gz")}, gzip},
        {"bz2", "BZIP2", {QStringLiteral("bz2")}, bzip2},
        {"xz", "XZ", {QStringLiteral("xz")}, xz},
        {"zst", "Zstandard", {QStringLiteral("zst")}, zstd},
        {"lz4", "LZ4", {QStringLiteral("lz4")}, lz4},
        {"lzma", "LZMA", {QStringLiteral("lzma")}, lzma},
    };
    for (const StreamVariant &v : streams) {
        FormatCapability c = make(QString::fromLatin1(v.id), QString::fromLatin1(v.label), v.ext);
        c.canRead = true;
        c.canCreate = libRaw && v.filterOk;
        c.readBackend = QStringLiteral("libarchive");
        c.writeBackend = QStringLiteral("libarchive");
        c.note = QStringLiteral("Single-file stream; creation accepts exactly one input file.");
        m_formats.append(c);
    }

    FormatCapability cpio = make(QStringLiteral("cpio"), QStringLiteral("CPIO"),
                                 {QStringLiteral("cpio")});
    cpio.canRead = true;
    cpio.canCreate = libCpio;
    cpio.readBackend = QStringLiteral("libarchive");
    cpio.writeBackend = QStringLiteral("libarchive");
    m_formats.append(cpio);

    FormatCapability cab = make(QStringLiteral("cab"), QStringLiteral("CAB"),
                                {QStringLiteral("cab")});
    cab.canRead = true; // libarchive reads CAB; 7-Zip is an additional fallback.
    cab.readBackend = QStringLiteral("libarchive / 7-Zip");
    cab.note = QStringLiteral("Read/extract only.");
    m_formats.append(cab);

    FormatCapability lha = make(QStringLiteral("lha"), QStringLiteral("LHA / LZH"),
                                {QStringLiteral("lha"), QStringLiteral("lzh")});
    lha.canRead = sevenZip && szHas(QStringLiteral("lha"));
    lha.readBackend = QStringLiteral("7-Zip");
    lha.note = QStringLiteral("Read/extract only, when the installed 7-Zip supports LHA.");
    m_formats.append(lha);

    FormatCapability arj = make(QStringLiteral("arj"), QStringLiteral("ARJ"),
                                {QStringLiteral("arj")});
    arj.canRead = sevenZip && szHas(QStringLiteral("arj"));
    arj.readBackend = QStringLiteral("7-Zip");
    arj.note = QStringLiteral("Read/extract only, when the installed 7-Zip supports ARJ.");
    m_formats.append(arj);

    FormatCapability rpm = make(QStringLiteral("rpm"), QStringLiteral("RPM / DEB"),
                                {QStringLiteral("rpm"), QStringLiteral("deb")});
    rpm.canRead = true;
    rpm.readBackend = QStringLiteral("libarchive");
    rpm.note = QStringLiteral("Inspection and payload extraction; never created.");
    m_formats.append(rpm);

    FormatCapability iso = make(QStringLiteral("iso"), QStringLiteral("ISO / UDF"),
                                {QStringLiteral("iso"), QStringLiteral("udf")});
    iso.canRead = true;
    iso.canCreate = xorrisoAvailable();
    iso.readBackend = QStringLiteral("libarchive / 7-Zip");
    iso.writeBackend = QStringLiteral("xorriso");
    iso.note = QStringLiteral("Creation and mounting are handled by the Disc Image engine.");
    m_formats.append(iso);

    FormatCapability img = make(QStringLiteral("img"), QStringLiteral("IMG / BIN"),
                                {QStringLiteral("img"), QStringLiteral("bin"),
                                 QStringLiteral("cue"), QStringLiteral("raw")});
    img.canRead = sevenZip && (szHas(QStringLiteral("fat")) || szHas(QStringLiteral("ntfs"))
                               || szHas(QStringLiteral("iso")));
    img.readBackend = QStringLiteral("7-Zip");
    img.note = QStringLiteral("Capability depends on filesystem detection; mounting is ISO-only.");
    m_formats.append(img);
}

QVector<FormatCapability> CapabilityRegistry::formats() const
{
    return m_formats;
}

FormatCapability CapabilityRegistry::format(const QString &id) const
{
    for (const FormatCapability &c : m_formats)
        if (c.id.compare(id, Qt::CaseInsensitive) == 0)
            return c;
    return FormatCapability();
}

bool CapabilityRegistry::canCreate(const QString &id) const
{
    return format(id).canCreate;
}

bool CapabilityRegistry::canEncrypt(const QString &id) const
{
    return format(id).canEncrypt;
}

QStringList CapabilityRegistry::creatableFormats() const
{
    QStringList result;
    for (const FormatCapability &c : m_formats)
        if (c.canCreate)
            result << c.id;
    return result;
}

QStringList CapabilityRegistry::readableFormats() const
{
    QStringList result;
    for (const FormatCapability &c : m_formats)
        if (c.canRead)
            result << c.id;
    return result;
}

QString CapabilityRegistry::formatForPath(const QString &path) const
{
    const QString name = QFileInfo(path).fileName().toLower();
    // Longest extension wins, so .tar.gz beats .gz.
    QString best;
    int bestLength = -1;
    for (const FormatCapability &c : m_formats) {
        for (const QString &ext : c.extensions) {
            if (name.endsWith(QLatin1Char('.') + ext) && ext.size() > bestLength) {
                best = c.id;
                bestLength = ext.size();
            }
        }
    }
    if (!best.isEmpty())
        return best;

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("jar") || suffix == QLatin1String("apk"))
        return QStringLiteral("zip");
    if (suffix == QLatin1String("tgz"))
        return QStringLiteral("tar.gz");
    if (suffix == QLatin1String("tbz2"))
        return QStringLiteral("tar.bz2");
    if (suffix == QLatin1String("txz"))
        return QStringLiteral("tar.xz");
    return QString();
}

QString CapabilityRegistry::report() const
{
    QStringList lines;
    lines << QStringLiteral("libarchive: %1").arg(QString::fromUtf8(archive_version_string()));
    lines << QStringLiteral("7-Zip: %1").arg(sevenZipAvailable() ? sevenZipPath()
                                                                : QStringLiteral("not installed"));
    lines << QStringLiteral("xorriso: %1").arg(xorrisoAvailable() ? xorrisoPath()
                                                                  : QStringLiteral("not installed"));
    lines << QStringLiteral("udisksctl: %1").arg(udisksctlAvailable() ? udisksctlPath()
                                                                      : QStringLiteral("not installed"));
    lines << QString();
    lines << QStringLiteral("%1  %2  %3  %4  %5")
                 .arg(QStringLiteral("FORMAT"), -10)
                 .arg(QStringLiteral("READ"), -6)
                 .arg(QStringLiteral("CREATE"), -8)
                 .arg(QStringLiteral("ENCRYPT"), -9)
                 .arg(QStringLiteral("BACKEND"));
    for (const FormatCapability &c : m_formats) {
        lines << QStringLiteral("%1  %2  %3  %4  %5")
                     .arg(c.id, -10)
                     .arg(c.canRead ? QStringLiteral("yes") : QStringLiteral("-"), -6)
                     .arg(c.canCreate ? QStringLiteral("yes") : QStringLiteral("-"), -8)
                     .arg(c.canEncrypt ? QStringLiteral("yes") : QStringLiteral("-"), -9)
                     .arg(c.writeBackend.isEmpty() ? c.readBackend : c.writeBackend);
    }
    return lines.join(QLatin1Char('\n'));
}

bool CapabilityRegistry::xorrisoAvailable()
{
    return !xorrisoPath().isEmpty();
}

bool CapabilityRegistry::udisksctlAvailable()
{
    return !udisksctlPath().isEmpty();
}

bool CapabilityRegistry::sevenZipAvailable()
{
    return !sevenZipPath().isEmpty();
}

QString CapabilityRegistry::xorrisoPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("xorriso"));
}

QString CapabilityRegistry::udisksctlPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("udisksctl"));
}

QString CapabilityRegistry::sevenZipPath()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(QStringLiteral("7za"));
    return path;
}

QStringList CapabilityRegistry::sevenZipFormatNames()
{
    static QStringList cached;
    static bool probed = false;
    if (probed)
        return cached;
    probed = true;

    const QString exe = sevenZipPath();
    if (exe.isEmpty())
        return cached;

    QProcess process;
    process.setProgram(exe);
    process.setArguments({QStringLiteral("i")});
    process.start();
    process.closeWriteChannel();
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return cached;
    }
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    // Format table lines look like: " 0 C...F.......... 7z 7z ..."
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1Char('0')))
            continue;
        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        // Some rows (7z, zip, tar, gzip, wim) carry a second flags column
        // (e.g. "w...0"); in that case the format name is the next token.
        int nameIndex = 2;
        if (nameIndex < parts.size() && parts.at(nameIndex).contains(QLatin1Char('.')))
            nameIndex = 3;
        if (nameIndex < parts.size()) {
            const QString name = parts.at(nameIndex).toLower();
            if (!name.isEmpty() && name != QLatin1String(":"))
                cached << name;
        }
    }
    cached.removeDuplicates();
    return cached;
}

} // namespace eis
