#include "archive/ArchiveService.h"
#include "backends/LibarchiveBackend.h"
#include "backends/SevenZipBackend.h"
#include "compression/CompressionEngine.h"
#include "core/Capabilities.h"
#include "core/Types.h"
#include "discimage/DiscImageEngine.h"
#include "vault/PasswordVault.h"
#include "verification/HashEngine.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace eis;

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(data) == data.size();
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

ExtractionResult extractWithLibarchive(const QString &archive, const QString &dest,
                                       const QString &password = QString())
{
    LibarchiveBackend backend;
    ExtractionOptions opts;
    opts.archivePath = archive;
    opts.destination = dest;
    opts.password = password;
    opts.collision = CollisionPolicy::Rename;
    JobControl control;
    return backend.extract(opts, control, nullptr);
}

} // namespace

class TestOperations : public QObject {
    Q_OBJECT
private slots:
    void capabilitiesReportIsNonEmpty();
    void compressionZipRoundtrip();
    void compression7zEncryptedRoundtrip();
    void vaultEncryptsAndRejectsWrongPassword();
    void vaultExportAndRestore();
    void checksumGenerateAndVerify();
    void isoCreateAndInspect();
    void libarchiveRejectsZeroFilledImage();
};

void TestOperations::capabilitiesReportIsNonEmpty()
{
    const QString report = CapabilityRegistry::instance().report();
    QVERIFY(report.contains(QStringLiteral("zip")));
    QVERIFY(report.contains(QStringLiteral("CREATE")));
    QVERIFY(CapabilityRegistry::instance().canCreate(QStringLiteral("zip")));
    QVERIFY(CapabilityRegistry::instance().canCreate(QStringLiteral("tar.gz")));
    if (CapabilityRegistry::sevenZipAvailable()) {
        QVERIFY(CapabilityRegistry::instance().canCreate(QStringLiteral("7z")));
        QVERIFY(CapabilityRegistry::instance().canEncrypt(QStringLiteral("7z")));
    }
}

void TestOperations::compressionZipRoundtrip()
{
    QTemporaryDir srcDir;
    QTemporaryDir outDir;
    QVERIFY(srcDir.isValid());
    QVERIFY(outDir.isValid());
    QVERIFY(writeFile(srcDir.path() + QStringLiteral("/alpha.txt"), QByteArray("alpha payload")));
    QVERIFY(writeFile(srcDir.path() + QStringLiteral("/beta.txt"), QByteArray("beta payload")));

    CompressionRequest request;
    request.sources = {srcDir.path() + QStringLiteral("/alpha.txt"),
                       srcDir.path() + QStringLiteral("/beta.txt")};
    request.format = QStringLiteral("zip");
    request.outputPath = outDir.path() + QStringLiteral("/bundle.zip");
    request.level = CompressionLevel::Fast;

    QString error;
    QVERIFY2(CompressionEngine::validate(request, &error), qPrintable(error));
    const CompressionResult result = CompressionEngine::create(request, nullptr, nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.filesAdded, 2);
    QVERIFY(QFile::exists(result.outputPath));
    QVERIFY(result.outputBytes > 0);

    QTemporaryDir destDir;
    QVERIFY(destDir.isValid());
    const ExtractionResult extracted = extractWithLibarchive(result.outputPath, destDir.path());
    QVERIFY2(extracted.ok, qPrintable(extracted.error));
    QCOMPARE(readAll(destDir.path() + QStringLiteral("/alpha.txt")), QByteArray("alpha payload"));
    QCOMPARE(readAll(destDir.path() + QStringLiteral("/beta.txt")), QByteArray("beta payload"));
}

void TestOperations::compression7zEncryptedRoundtrip()
{
    if (!CapabilityRegistry::sevenZipAvailable())
        QSKIP("7z not available");

    QTemporaryDir srcDir;
    QTemporaryDir outDir;
    QVERIFY(srcDir.isValid());
    QVERIFY(outDir.isValid());
    QVERIFY(writeFile(srcDir.path() + QStringLiteral("/secret.txt"), QByteArray("classified data")));

    CompressionRequest request;
    request.sources = {srcDir.path() + QStringLiteral("/secret.txt")};
    request.format = QStringLiteral("7z");
    request.outputPath = outDir.path() + QStringLiteral("/secure.7z");
    request.password = QStringLiteral("Correct-Horse-9!");
    request.encryptHeaders = true;

    QString error;
    QVERIFY2(CompressionEngine::validate(request, &error), qPrintable(error));
    const CompressionResult result = CompressionEngine::create(request, nullptr, nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.encrypted);

    QTemporaryDir destDir;
    QVERIFY(destDir.isValid());
    SevenZipBackend backend;
    ExtractionOptions opts;
    opts.archivePath = result.outputPath;
    opts.destination = destDir.path();
    opts.password = request.password;
    opts.collision = CollisionPolicy::Rename;
    JobControl control;
    const ExtractionResult extracted = backend.extract(opts, control, nullptr);
    QVERIFY2(extracted.ok, qPrintable(extracted.error));
    QStringList extractedFiles;
    QDirIterator it(destDir.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        extractedFiles << QDir(destDir.path()).relativeFilePath(it.next());
    QVERIFY2(extractedFiles.contains(QStringLiteral("secret.txt")),
             qPrintable(QStringLiteral("extracted: ") + extractedFiles.join(QStringLiteral(", "))));
    const QFileInfo extractedInfo(destDir.path() + QStringLiteral("/secret.txt"));
    QVERIFY2(extractedInfo.size() == 15,
             qPrintable(QStringLiteral("size=%1 perms=%2 readable=%3")
                            .arg(extractedInfo.size())
                            .arg(int(extractedInfo.permissions()))
                            .arg(extractedInfo.isReadable())));
    QFile direct(destDir.path() + QStringLiteral("/secret.txt"));
    QVERIFY2(direct.open(QIODevice::ReadOnly), qPrintable(direct.errorString()));
    const QByteArray directData = direct.readAll();
    QVERIFY2(directData == QByteArray("classified data"),
             qPrintable(QStringLiteral("read %1 bytes: %2")
                            .arg(directData.size())
                            .arg(QString::fromLatin1(directData.toHex()))));

    QTemporaryDir wrongDest;
    QVERIFY(wrongDest.isValid());
    opts.destination = wrongDest.path();
    opts.password = QStringLiteral("wrong-password");
    JobControl wrongControl;
    const ExtractionResult wrong = backend.extract(opts, wrongControl, nullptr);
    QVERIFY(!wrong.ok);
    QVERIFY(!QFile::exists(wrongDest.path() + QStringLiteral("/secret.txt")));
}

void TestOperations::vaultEncryptsAndRejectsWrongPassword()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString vaultPath = dir.path() + QStringLiteral("/session.eisvlt");
    const QString master = QStringLiteral("Master-Passphrase-42");
    const QString secret = QStringLiteral("Sup3rSecretArchivePassword");

    PasswordVault vault;
    QString error;
    QVERIFY2(vault.create(vaultPath, master, &error), qPrintable(error));

    VaultEntry entry;
    entry.type = QStringLiteral("website");
    entry.title = QStringLiteral("Test site");
    entry.username = QStringLiteral("tester");
    entry.password = secret;
    QVERIFY2(vault.addEntry(entry, &error), qPrintable(error));

    ArchivePasswordRecord record;
    record.archiveName = QStringLiteral("backup.zip");
    record.archivePath = QStringLiteral("/tmp/backup.zip");
    record.password = secret;
    QVERIFY2(vault.addArchivePassword(record, &error), qPrintable(error));
    QVERIFY2(vault.save(&error), qPrintable(error));

    const QByteArray raw = readAll(vaultPath);
    QVERIFY(!raw.isEmpty());
    QVERIFY(raw.startsWith("EISVLT01"));
    QVERIFY(!raw.contains(secret.toUtf8()));
    QVERIFY(!raw.contains(master.toUtf8()));
    QCOMPARE(QFileInfo(vaultPath).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
             QFileDevice::Permissions());

    vault.lock();
    QVERIFY(!vault.isUnlocked());
    QVERIFY(!vault.unlock(vaultPath, QStringLiteral("definitely-wrong"), &error));

    QVERIFY2(vault.unlock(vaultPath, master, &error), qPrintable(error));
    QCOMPARE(vault.entryCount(), 1);
    QCOMPARE(vault.archivePasswordCount(), 1);
    QCOMPARE(vault.entries().first().password, secret);
    const QVector<ArchivePasswordRecord> matches = vault.passwordsForArchive(record.archivePath);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().password, secret);
}

void TestOperations::vaultExportAndRestore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.path() + QStringLiteral("/source.eisvlt");
    const QString backupPath = dir.path() + QStringLiteral("/backup.eisvlt");
    const QString targetPath = dir.path() + QStringLiteral("/target.eisvlt");
    const QString sourceMaster = QStringLiteral("Source-Master-11");
    const QString targetMaster = QStringLiteral("Target-Master-22");
    const QString secret = QStringLiteral("restored-secret-value");

    PasswordVault source;
    QString error;
    QVERIFY2(source.create(sourcePath, sourceMaster, &error), qPrintable(error));
    VaultEntry entry;
    entry.title = QStringLiteral("Backup entry");
    entry.password = secret;
    QVERIFY2(source.addEntry(entry, &error), qPrintable(error));
    QVERIFY2(source.save(&error), qPrintable(error));
    QVERIFY2(source.exportEncrypted(backupPath, &error), qPrintable(error));
    source.lock();

    PasswordVault target;
    QVERIFY2(target.create(targetPath, targetMaster, &error), qPrintable(error));
    QVERIFY2(target.restoreEncrypted(backupPath, sourceMaster, &error), qPrintable(error));
    QCOMPARE(target.entryCount(), 1);
    QCOMPARE(target.entries().first().password, secret);
    QVERIFY(!target.restoreEncrypted(backupPath, QStringLiteral("wrong-master"), &error));

    target.lock();
    QVERIFY2(target.unlock(targetPath, targetMaster, &error), qPrintable(error));
    QCOMPARE(target.entryCount(), 1);
    QCOMPARE(target.entries().first().password, secret);
}

void TestOperations::checksumGenerateAndVerify()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dataPath = dir.path() + QStringLiteral("/data.bin");
    QVERIFY(writeFile(dataPath, QByteArray(4096, 'A')));

    const HashResult hashed = HashEngine::hashFile(dataPath, HashAlgorithm::Sha256);
    QVERIFY2(hashed.ok, qPrintable(hashed.error));
    QCOMPARE(hashed.digest.size(), 64);

    const QString checksumPath = dir.path() + QStringLiteral("/data.sha256");
    QString error;
    QVERIFY2(HashEngine::generateChecksumFile({dataPath}, checksumPath, HashAlgorithm::Sha256,
                                              &error),
             qPrintable(error));

    const ChecksumFileResult good = HashEngine::verifyChecksumFile(checksumPath);
    QVERIFY2(good.ok, qPrintable(good.errors.join(QStringLiteral("; "))));
    QCOMPARE(good.checked, 1);
    QCOMPARE(good.matched, 1);

    QVERIFY(writeFile(dataPath, QByteArray(4096, 'B')));
    const ChecksumFileResult tampered = HashEngine::verifyChecksumFile(checksumPath);
    QVERIFY(!tampered.ok);
    QCOMPARE(tampered.mismatched, 1);
}

void TestOperations::isoCreateAndInspect()
{
    if (!DiscImageEngine::canCreateIso())
        QSKIP("xorriso not available");

    QTemporaryDir srcDir;
    QTemporaryDir outDir;
    QVERIFY(srcDir.isValid());
    QVERIFY(outDir.isValid());
    QVERIFY(writeFile(srcDir.path() + QStringLiteral("/readme.txt"), QByteArray("iso payload")));

    const QString isoPath = outDir.path() + QStringLiteral("/image.iso");
    DiscImageEngine engine;
    QString error;
    QVERIFY2(engine.createIso(srcDir.path(), isoPath, QStringLiteral("EIS_TEST"), &error),
             qPrintable(error));
    QVERIFY(QFile::exists(isoPath));

    const DiscImageInfo info = engine.inspect(isoPath);
    QVERIFY2(info.ok, qPrintable(info.error));
    QVERIFY(info.isoLike);
    QVERIFY(info.readable);
    QVERIFY(info.listing.entries.size() >= 1);
}

void TestOperations::libarchiveRejectsZeroFilledImage()
{
    // UDF/ISO images begin with zeroed system sectors. libarchive's TAR reader
    // used to report these as a successful empty archive, hiding the entries
    // that 7-Zip can read. A multi-megabyte zero-filled file must not probe as
    // an archive through libarchive.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = dir.path() + QStringLiteral("/zero-filled.iso");
    QFile file(imagePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(2 * 1024 * 1024));
    file.close();

    LibarchiveBackend backend;
    QString format;
    QString error;
    QVERIFY(!backend.probe(imagePath, QString(), &format, &error));
}

QTEST_MAIN(TestOperations)
#include "test_operations.moc"
