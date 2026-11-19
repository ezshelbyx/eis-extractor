#include "ui/MainWindow.h"
#include "ui/ProgressWindow.h"

#include "archive/ArchiveService.h"
#include "backends/BackendRegistry.h"
#include "compression/CompressionEngine.h"
#include "core/Capabilities.h"
#include "core/Types.h"
#include "core/Utils.h"
#include "discimage/DiscImageEngine.h"
#include "logging/Logger.h"
#include "settings/AppSettings.h"
#include "vault/PasswordVault.h"
#include "verification/HashEngine.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <memory>

class ExtractionWorker : public QObject {
    Q_OBJECT
public:
    ExtractionWorker(const QStringList &files, const QStringList &dests)
        : m_files(files), m_dests(dests) {}

public slots:
    void run() {
        bool overallOk = true;
        const int total = m_files.size();
        for (int i = 0; i < total; ++i) {
            emit archiveStarted(i, QFileInfo(m_files[i]).fileName(), m_dests.value(i));
            eis::BackendRegistry registry;
            QString format, error;
            auto backend = registry.select(m_files[i], QString(), &format, &error);
            if (!backend) {
                emit archiveFinished(false, error.isEmpty() ? QStringLiteral("No backend available") : error);
                overallOk = false;
                continue;
            }
            eis::ExtractionOptions opts;
            opts.archivePath = m_files[i];
            opts.destination = m_dests.value(i);
            opts.collision = eis::CollisionPolicy::Rename;
            opts.preservePermissions = eis::AppSettings::instance().preservePermissions();
            opts.maxEntries = eis::AppSettings::instance().maxEntries();
            opts.maxTotalBytes = eis::AppSettings::instance().maxTotalBytes();
            m_control.cancel.store(false);
            m_control.pause.store(false);
            m_currentControl = &m_control;
            const eis::ExtractionResult res = backend->extract(opts, m_control, [this, i](const eis::ProgressInfo &p) {
                emit progress(p);
            });
            m_currentControl = nullptr;
            emit archiveFinished(res.ok, res.error);
            if (!res.ok)
                overallOk = false;
            if (m_control.cancel.load())
                break;
        }
        emit allFinished(overallOk);
    }

    void cancel() {
        if (m_currentControl)
            m_currentControl->cancel.store(true);
    }

signals:
    void archiveStarted(int index, QString name, QString dest);
    void progress(eis::ProgressInfo info);
    void archiveFinished(bool ok, QString error);
    void allFinished(bool ok);

private:
    QStringList m_files;
    QStringList m_dests;
    eis::JobControl m_control;
    eis::JobControl *m_currentControl = nullptr;
};

static QStringList gatherFiles(const QCommandLineParser &parser, const QString &optionName)
{
    QStringList files;
    if (parser.isSet(optionName)) {
        const QString v = parser.value(optionName);
        if (!v.isEmpty())
            files << v;
    }
    files << parser.positionalArguments();
    files.removeDuplicates();
    return files;
}

static int runExtraction(const QStringList &files, bool newFolder)
{
    int rc = 0;
    for (const QString &archive : files) {
        QFileInfo fi(archive);
        QString dest = fi.absolutePath();
        if (newFolder)
            dest = fi.absolutePath() + QLatin1Char('/') + eis::utils::stripArchiveExtensions(fi.fileName());
        eis::BackendRegistry registry;
        QString format, error;
        auto backend = registry.select(archive, QString(), &format, &error);
        if (!backend) {
            eis::Logger::instance().error(error.isEmpty() ? QStringLiteral("No backend available") : error);
            rc = 1;
            continue;
        }
        eis::ExtractionOptions opts;
        opts.archivePath = archive;
        opts.destination = dest;
        opts.collision = eis::CollisionPolicy::Rename;
        opts.preservePermissions = eis::AppSettings::instance().preservePermissions();
        opts.maxEntries = eis::AppSettings::instance().maxEntries();
        opts.maxTotalBytes = eis::AppSettings::instance().maxTotalBytes();
        eis::JobControl control;
        const eis::ExtractionResult res = backend->extract(opts, control, nullptr);
        if (res.ok) {
            eis::Logger::instance().success(QStringLiteral("Extraction completed: %1").arg(dest));
        } else {
            eis::Logger::instance().error(res.error);
            rc = 1;
        }
    }
    return rc;
}

static int runCompress(const QStringList &files, const QString &format)
{
    if (files.isEmpty()) {
        eis::Logger::instance().error(QStringLiteral("no input files"));
        return 1;
    }
    eis::CompressionRequest request;
    request.sources = files;
    request.format = format;
    request.level = eis::CompressionLevel::Normal;
    request.outputPath = eis::CompressionEngine::suggestOutputPath(files, format);

    QString error;
    if (!eis::CompressionEngine::validate(request, &error)) {
        eis::Logger::instance().error(error);
        return 1;
    }
    const eis::CompressionResult result = eis::CompressionEngine::create(request, nullptr, nullptr);
    if (!result.ok) {
        eis::Logger::instance().error(result.error);
        return 1;
    }
    eis::Logger::instance().success(
        QStringLiteral("Created %1 (%2 files, %3)")
            .arg(result.outputPath)
            .arg(result.filesAdded)
            .arg(eis::utils::humanBytes(result.outputBytes)));
    return 0;
}

static int runChecksums(const QStringList &files, const QString &algorithmName)
{
    bool ok = false;
    const eis::HashAlgorithm algorithm = eis::hashAlgorithmFromName(algorithmName, &ok);
    if (!ok) {
        eis::Logger::instance().error(QStringLiteral("unknown hash algorithm: %1").arg(algorithmName));
        return 2;
    }
    int rc = 0;
    for (const QString &file : files) {
        const eis::HashResult result = eis::HashEngine::hashFile(file, algorithm);
        if (!result.ok) {
            printf("ERROR  %s  %s\n", qPrintable(file), qPrintable(result.error));
            rc = 1;
            continue;
        }
        printf("%s  %s\n", qPrintable(result.digest), qPrintable(file));
    }
    return rc;
}

static int runVerifyChecksums(const QString &checksumFile)
{
    const eis::ChecksumFileResult result = eis::HashEngine::verifyChecksumFile(checksumFile);
    printf("checked=%d matched=%d mismatched=%d missing=%d\n", result.checked, result.matched,
           result.mismatched, result.missing);
    for (const QString &error : result.errors)
        printf("ERROR  %s\n", qPrintable(error));
    for (const QString &mismatch : result.mismatches)
        printf("MISMATCH  %s\n", qPrintable(mismatch));
    printf("%s\n", result.ok ? "all checksums match" : "verification failed");
    return result.ok ? 0 : 1;
}

static int runTest(const QStringList &files)
{
    int rc = 0;
    for (const QString &archive : files) {
        eis::ArchiveService service;
        const eis::IntegrityResult res = service.test(archive, QString());
        if (res.ok) {
            eis::Logger::instance().success(QStringLiteral("Integrity OK: %1").arg(archive));
        } else {
            eis::Logger::instance().error(res.error.isEmpty() ? QStringLiteral("Integrity failed: %1").arg(archive) : res.error);
            rc = 1;
        }
    }
    return rc;
}

static int runExtractTo(const QStringList &files)
{
    int rc = 0;
    for (const QString &archive : files) {
        const QString dest = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Extract to..."), QFileInfo(archive).absolutePath());
        if (dest.isEmpty()) {
            rc = 1;
            continue;
        }
        eis::BackendRegistry registry;
        QString format, error;
        auto backend = registry.select(archive, QString(), &format, &error);
        if (!backend) {
            eis::Logger::instance().error(error.isEmpty() ? QStringLiteral("No backend available") : error);
            rc = 1;
            continue;
        }
        eis::ExtractionOptions opts;
        opts.archivePath = archive;
        opts.destination = dest;
        opts.collision = eis::CollisionPolicy::Rename;
        opts.preservePermissions = eis::AppSettings::instance().preservePermissions();
        opts.maxEntries = eis::AppSettings::instance().maxEntries();
        opts.maxTotalBytes = eis::AppSettings::instance().maxTotalBytes();
        eis::JobControl control;
        const eis::ExtractionResult res = backend->extract(opts, control, nullptr);
        if (res.ok) {
            eis::Logger::instance().success(QStringLiteral("Extraction completed: %1").arg(dest));
        } else {
            eis::Logger::instance().error(res.error);
            rc = 1;
        }
    }
    return rc;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("E.I.S ARCHIVE"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));
    app.setOrganizationName(QStringLiteral("E.I.S CYBERCORE"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("E.I.S EXTRACTOR"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smokeOption(QStringList() << "smoke", QStringLiteral("Run self-check and exit."));
    parser.addOption(smokeOption);
    QCommandLineOption themeOption(QStringList() << "theme", QStringLiteral("Theme."), QStringLiteral("theme"), QString());
    parser.addOption(themeOption);
    QCommandLineOption progressOption(QStringList() << "progress", QStringLiteral("Show progress window."));
    parser.addOption(progressOption);
    QCommandLineOption extractHereOption(QStringList() << "extract-here", QStringLiteral("Extract archive to its own directory."), QStringLiteral("archive"));
    parser.addOption(extractHereOption);
    QCommandLineOption extractNewOption(QStringList() << "extract-new-folder", QStringLiteral("Extract archive into a new folder."), QStringLiteral("archive"));
    parser.addOption(extractNewOption);
    QCommandLineOption extractToOption(QStringList() << "extract-to", QStringLiteral("Extract archive to a chosen directory."), QStringLiteral("archive"));
    parser.addOption(extractToOption);
    QCommandLineOption extractSelectedOption(QStringList() << "extract-selected", QStringLiteral("Extract selected archives."), QStringLiteral("archive"));
    parser.addOption(extractSelectedOption);
    QCommandLineOption testOption(QStringList() << "test", QStringLiteral("Test archive integrity."), QStringLiteral("archive"));
    parser.addOption(testOption);
    QCommandLineOption inspectOption(QStringList() << "inspect", QStringLiteral("Open archive in inspector."), QStringLiteral("archive"));
    parser.addOption(inspectOption);

    QCommandLineOption compressZipOption(QStringList() << "compress-zip",
                                         QStringLiteral("Create a ZIP archive from positional files."));
    parser.addOption(compressZipOption);
    QCommandLineOption compress7zOption(QStringList() << "compress-7z",
                                        QStringLiteral("Create a 7Z archive from positional files."));
    parser.addOption(compress7zOption);
    QCommandLineOption checksumOption(QStringList() << "checksum",
                                      QStringLiteral("Print checksums for positional files."));
    parser.addOption(checksumOption);
    QCommandLineOption checksumWriteOption(QStringList() << "checksum-write",
                                           QStringLiteral("Write a .sha256 checksum file next to a file."),
                                           QStringLiteral("file"));
    parser.addOption(checksumWriteOption);
    QCommandLineOption checksumAlgoOption(QStringList() << "checksum-algo",
                                          QStringLiteral("SHA-256, SHA-512, SHA-1 or MD5."),
                                          QStringLiteral("algorithm"), QStringLiteral("SHA-256"));
    parser.addOption(checksumAlgoOption);
    QCommandLineOption verifyChecksumsOption(QStringList() << "verify-checksums",
                                             QStringLiteral("Verify a checksum file."),
                                             QStringLiteral("file"));
    parser.addOption(verifyChecksumsOption);
    QCommandLineOption mountImageOption(QStringList() << "mount-image",
                                        QStringLiteral("Mount an ISO read-only via udisks2."),
                                        QStringLiteral("image"));
    parser.addOption(mountImageOption);
    QCommandLineOption unmountImageOption(QStringList() << "unmount-image",
                                          QStringLiteral("Unmount an E.I.S-managed image mount."),
                                          QStringLiteral("image-or-device"));
    parser.addOption(unmountImageOption);
    QCommandLineOption createIsoOption(QStringList() << "create-iso",
                                       QStringLiteral("Create an ISO from a directory."),
                                       QStringLiteral("directory"));
    parser.addOption(createIsoOption);
    QCommandLineOption isoOutputOption(QStringList() << "iso-output",
                                       QStringLiteral("Output path for --create-iso."),
                                       QStringLiteral("file"));
    parser.addOption(isoOutputOption);
    QCommandLineOption isoLabelOption(QStringList() << "iso-label",
                                      QStringLiteral("Volume label for --create-iso."),
                                      QStringLiteral("label"), QStringLiteral("EIS_IMAGE"));
    parser.addOption(isoLabelOption);
    QCommandLineOption capabilitiesOption(QStringList() << "capabilities",
                                          QStringLiteral("Print the probed capability registry and exit."));
    parser.addOption(capabilitiesOption);
    parser.process(app);

    if (parser.isSet(themeOption))
        eis::AppSettings::instance().setTheme(parser.value(themeOption));

    if (parser.isSet(capabilitiesOption)) {
        printf("%s\n", qPrintable(eis::CapabilityRegistry::instance().report()));
        return 0;
    }

    if (parser.isSet(smokeOption)) {
        eis::Logger::instance().info(QStringLiteral("Smoke check: application initialized."));
        eis::Logger::instance().info(QStringLiteral("Smoke check: settings loaded."));
        eis::Logger::instance().info(QStringLiteral("Smoke check: capabilities probed."));
        eis::Logger::instance().info(QStringLiteral("Smoke check: OK"));
        return 0;
    }

    if (parser.isSet(compressZipOption))
        return runCompress(parser.positionalArguments(), QStringLiteral("zip"));
    if (parser.isSet(compress7zOption))
        return runCompress(parser.positionalArguments(), QStringLiteral("7z"));
    if (parser.isSet(checksumOption))
        return runChecksums(parser.positionalArguments(), parser.value(checksumAlgoOption));
    if (parser.isSet(checksumWriteOption)) {
        const QString file = parser.value(checksumWriteOption);
        const QString output = file + QStringLiteral(".sha256");
        QString error;
        if (!eis::HashEngine::generateChecksumFile({file}, output, eis::HashAlgorithm::Sha256,
                                                   &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        printf("created %s\n", qPrintable(output));
        return 0;
    }
    if (parser.isSet(verifyChecksumsOption))
        return runVerifyChecksums(parser.value(verifyChecksumsOption));

    if (parser.isSet(mountImageOption)) {
        eis::DiscImageEngine engine;
        const eis::MountResult result = engine.mount(parser.value(mountImageOption));
        if (!result.ok) {
            fprintf(stderr, "%s\n", qPrintable(result.error));
            return 1;
        }
        printf("%s %s\n", qPrintable(result.device), qPrintable(result.mountPoint));
        return 0;
    }
    if (parser.isSet(unmountImageOption)) {
        eis::DiscImageEngine engine;
        QString error;
        if (!engine.unmount(parser.value(unmountImageOption), &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        printf("unmounted\n");
        return 0;
    }
    if (parser.isSet(createIsoOption)) {
        if (!parser.isSet(isoOutputOption)) {
            fprintf(stderr, "--create-iso requires --iso-output <file>\n");
            return 2;
        }
        eis::DiscImageEngine engine;
        QString error;
        if (!engine.createIso(parser.value(createIsoOption), parser.value(isoOutputOption),
                              parser.value(isoLabelOption), &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        printf("created %s\n", qPrintable(parser.value(isoOutputOption)));
        return 0;
    }

    const bool progress = parser.isSet(progressOption);

    if (parser.isSet(extractHereOption) && !progress) {
        QStringList files = gatherFiles(parser, QStringLiteral("extract-here"));
        if (files.isEmpty()) files = parser.positionalArguments();
        return runExtraction(files, false);
    }
    if (parser.isSet(extractNewOption) && !progress) {
        QStringList files = gatherFiles(parser, QStringLiteral("extract-new-folder"));
        if (files.isEmpty()) files = parser.positionalArguments();
        return runExtraction(files, true);
    }
    if (parser.isSet(extractSelectedOption) && !progress) {
        QStringList files = gatherFiles(parser, QStringLiteral("extract-selected"));
        if (files.isEmpty()) files = parser.positionalArguments();
        return runExtraction(files, true);
    }
    if (parser.isSet(extractToOption) && !progress) {
        QStringList files = gatherFiles(parser, QStringLiteral("extract-to"));
        if (files.isEmpty()) files = parser.positionalArguments();
        return runExtractTo(files);
    }
    if (parser.isSet(testOption) && !progress) {
        QStringList files = gatherFiles(parser, QStringLiteral("test"));
        if (files.isEmpty()) files = parser.positionalArguments();
        return runTest(files);
    }

    // Progress mode or GUI
    bool newFolder = parser.isSet(extractNewOption) || parser.isSet(extractSelectedOption);
    bool extractTo = parser.isSet(extractToOption);

    if (progress || parser.isSet(extractHereOption) || parser.isSet(extractNewOption) || parser.isSet(extractSelectedOption) || parser.isSet(extractToOption)) {
        QStringList files;
        if (parser.isSet(extractHereOption)) files = gatherFiles(parser, QStringLiteral("extract-here"));
        else if (parser.isSet(extractNewOption)) files = gatherFiles(parser, QStringLiteral("extract-new-folder"));
        else if (parser.isSet(extractSelectedOption)) files = gatherFiles(parser, QStringLiteral("extract-selected"));
        else if (parser.isSet(extractToOption)) files = gatherFiles(parser, QStringLiteral("extract-to"));
        if (files.isEmpty()) files = parser.positionalArguments();
        if (files.isEmpty()) {
            // Fall back to GUI
        } else {
            QStringList dests;
            for (const QString &archive : files) {
                QFileInfo fi(archive);
                if (extractTo) {
                    const QString d = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Extract to..."), fi.absolutePath());
                    if (d.isEmpty()) { dests.clear(); break; }
                    dests << d;
                } else if (newFolder) {
                    dests << (fi.absolutePath() + QLatin1Char('/') + eis::utils::stripArchiveExtensions(fi.fileName()));
                } else {
                    dests << fi.absolutePath();
                }
            }
            if (!dests.isEmpty() && dests.size() == files.size()) {
                eis::ui::ProgressWindow window;
                window.show();
                QThread thread;
                ExtractionWorker worker(files, dests);
                worker.moveToThread(&thread);
                QObject::connect(&thread, &QThread::started, &worker, &ExtractionWorker::run);
                QObject::connect(&worker, &ExtractionWorker::archiveStarted, &window, &eis::ui::ProgressWindow::setArchiveInfo);
                QObject::connect(&worker, &ExtractionWorker::progress, &window, &eis::ui::ProgressWindow::onProgress);
                QObject::connect(&worker, &ExtractionWorker::archiveFinished, &window, &eis::ui::ProgressWindow::onArchiveFinished);
                QObject::connect(&worker, &ExtractionWorker::allFinished, &window, &eis::ui::ProgressWindow::onAllFinished);
                QObject::connect(&window, &eis::ui::ProgressWindow::cancelRequested, &worker, &ExtractionWorker::cancel);
                QObject::connect(&worker, &ExtractionWorker::allFinished, &thread, &QThread::quit);
                QObject::connect(&worker, &ExtractionWorker::allFinished, &app, [&window, &app](bool) {
                    if (!window.isVisible())
                        app.quit();
                });
                thread.start();
                return app.exec();
            }
        }
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        eis::DiscImageEngine engine;
        QString error;
        engine.unmountAll(&error);
        eis::PasswordVault::instance().lock();
    });

    eis::ui::MainWindow window;
    const QStringList pos = parser.positionalArguments();
    if (parser.isSet(inspectOption)) {
        const QString v = parser.value(inspectOption);
        if (!v.isEmpty())
            window.openArchive(v);
        else if (!pos.isEmpty())
            window.openArchive(pos.first());
    } else if (!pos.isEmpty()) {
        window.openArchive(pos.first());
    }
    window.show();
    return app.exec();
}

#include "main.moc"
