#include "backends/LibarchiveBackend.h"
#include "backends/SevenZipBackend.h"
#include "core/Types.h"
#include "security/PathGuard.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>

using namespace eis;

class TestEngine : public QObject {
    Q_OBJECT
private slots:
    void pathGuardRejectsTraversal();
    void pathGuardRejectsAbsolute();
    void libarchiveExtractsTar();
    void libarchiveRejectsTraversal();
    void sevenZipExtractsZip();
};

void TestEngine::pathGuardRejectsTraversal()
{
    QString err;
    const QString r = PathGuard::normalizeEntryPath(QStringLiteral("../evil.txt"), &err);
    QVERIFY(r.isEmpty());
    QVERIFY(!err.isEmpty());
}

void TestEngine::pathGuardRejectsAbsolute()
{
    QString err2;
    const QString r2 = PathGuard::normalizeEntryPath(QStringLiteral("/etc/passwd"), &err2);
    QVERIFY(r2.isEmpty());
    QVERIFY(!err2.isEmpty());
}

void TestEngine::libarchiveExtractsTar()
{
    QTemporaryDir srcDir;
    QVERIFY(srcDir.isValid());
    QFile f(srcDir.path() + QStringLiteral("/hello.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello world");
    f.close();

    QTemporaryDir workDir;
    QVERIFY(workDir.isValid());
    const QString archive = workDir.path() + QStringLiteral("/test.tar.gz");
    QProcess tar;
    tar.setWorkingDirectory(srcDir.path());
    tar.start(QStringLiteral("tar"), QStringList() << "-czf" << archive << ".");
    QVERIFY(tar.waitForFinished(10000));
    QCOMPARE(tar.exitCode(), 0);

    QTemporaryDir destDir;
    QVERIFY(destDir.isValid());
    LibarchiveBackend backend;
    ExtractionOptions opts;
    opts.archivePath = archive;
    opts.destination = destDir.path();
    opts.collision = CollisionPolicy::Rename;
    JobControl control;
    const ExtractionResult res = backend.extract(opts, control, nullptr);
    QVERIFY2(res.ok, qPrintable(res.error));
    QVERIFY(QFile::exists(destDir.path() + QStringLiteral("/hello.txt")));
}

void TestEngine::libarchiveRejectsTraversal()
{
    QTemporaryDir workDir;
    QVERIFY(workDir.isValid());
    const QString archive = workDir.path() + QStringLiteral("/evil.tar");
    // Build tar with a traversal entry using Python
    QProcess py;
    py.start(QStringLiteral("python3"), QStringList() << "-c"
        << QStringLiteral("import tarfile,io; tf=tarfile.open('%1','w'); ti=tarfile.TarInfo('../evil.txt'); ti.size=4; tf.addfile(ti, io.BytesIO(b'evil')); tf.close()").arg(archive));
    QVERIFY(py.waitForFinished(10000));
    QCOMPARE(py.exitCode(), 0);

    QTemporaryDir destDir;
    QVERIFY(destDir.isValid());
    LibarchiveBackend backend;
    ExtractionOptions opts;
    opts.archivePath = archive;
    opts.destination = destDir.path();
    opts.collision = CollisionPolicy::Rename;
    JobControl control;
    const ExtractionResult res = backend.extract(opts, control, nullptr);
    // Should not create evil.txt outside dest
    QVERIFY(!QFile::exists(QDir(destDir.path()).filePath(QStringLiteral("../evil.txt"))));
    QVERIFY(!QFile::exists(QDir::temp().filePath(QStringLiteral("evil.txt"))));
    // Result may be ok with skipped entries; ensure no traversal file
    Q_UNUSED(res);
}

void TestEngine::sevenZipExtractsZip()
{
    QTemporaryDir srcDir;
    QVERIFY(srcDir.isValid());
    QFile f(srcDir.path() + QStringLiteral("/hello.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello zip");
    f.close();

    QTemporaryDir workDir;
    QVERIFY(workDir.isValid());
    const QString archive = workDir.path() + QStringLiteral("/test.zip");
    QProcess zip;
    zip.setWorkingDirectory(srcDir.path());
    zip.start(QStringLiteral("zip"), QStringList() << "-q" << archive << "hello.txt");
    QVERIFY(zip.waitForFinished(10000));
    QCOMPARE(zip.exitCode(), 0);

    QTemporaryDir destDir;
    QVERIFY(destDir.isValid());
    SevenZipBackend backend;
    if (!backend.available()) {
        QSKIP("7z not available");
    }
    ExtractionOptions opts;
    opts.archivePath = archive;
    opts.destination = destDir.path();
    opts.collision = CollisionPolicy::Rename;
    JobControl control;
    const ExtractionResult res = backend.extract(opts, control, nullptr);
    QVERIFY2(res.ok, qPrintable(res.error));
    QVERIFY(QFile::exists(destDir.path() + QStringLiteral("/hello.txt")));
}

QTEST_MAIN(TestEngine)
#include "test_engine.moc"
