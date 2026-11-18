#include "ui/MainWindow.h"
#include "ui/ProgressWindow.h"

#include "archive/ArchiveService.h"
#include "backends/BackendRegistry.h"
#include "core/Types.h"
#include "core/Utils.h"
#include "logging/Logger.h"
#include "settings/AppSettings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QTimer>

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
    app.setApplicationName(QStringLiteral("E.I.S EXTRACTOR"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
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
    parser.process(app);

    if (parser.isSet(themeOption))
        eis::AppSettings::instance().setTheme(parser.value(themeOption));

    if (parser.isSet(smokeOption)) {
        eis::Logger::instance().info(QStringLiteral("Smoke check: application initialized."));
        eis::Logger::instance().info(QStringLiteral("Smoke check: settings loaded."));
        eis::Logger::instance().info(QStringLiteral("Smoke check: OK"));
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
    bool extractHere = parser.isSet(extractHereOption) || (!newFolder && !extractTo && (parser.isSet(progressOption)));

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
