#include "ui/DiscImagePage.h"

#include "archive/ArchiveService.h"
#include "core/Utils.h"
#include "queue/JobQueue.h"
#include "settings/AppSettings.h"
#include "verification/HashEngine.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QThread>
#include <QTreeView>
#include <QVBoxLayout>

#include <memory>

namespace eis::ui {

namespace {

class ImageWorker : public QObject {
    Q_OBJECT
public:
    enum class Mode { CreateIso, Hash };

    ImageWorker(Mode mode, QString source, QString target, QString label)
        : m_mode(mode)
        , m_source(std::move(source))
        , m_target(std::move(target))
        , m_label(std::move(label))
    {
    }

public slots:
    void run()
    {
        if (m_mode == Mode::CreateIso) {
            DiscImageEngine engine;
            JobControl control;
            QString error;
            const bool ok = engine.createIso(m_source, m_target, m_label, &error, &control);
            emit finished(ok, ok ? QStringLiteral("ISO created: %1").arg(m_target) : error);
            return;
        }

        const HashResult result = HashEngine::hashFile(m_target, HashAlgorithm::Sha256);
        if (!result.ok) {
            emit finished(false, result.error);
            return;
        }
        emit finished(true, QStringLiteral("SHA-256  %1\n%2")
                                .arg(result.digest, result.path));
    }

signals:
    void finished(bool ok, QString message);

private:
    Mode m_mode;
    QString m_source;
    QString m_target;
    QString m_label;
};

} // namespace

DiscImagePage::DiscImagePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("DISC IMAGES"), this);
    title->setObjectName("title");
    layout->addWidget(title);

    auto *inputGroup = new QGroupBox(QStringLiteral("IMAGE"), this);
    auto *form = new QFormLayout(inputGroup);
    auto *pathRow = new QHBoxLayout();
    m_imageEdit = new QLineEdit(inputGroup);
    m_imageEdit->setPlaceholderText(QStringLiteral("ISO, IMG, BIN/CUE, DMG..."));
    auto *browseButton = new QPushButton(QStringLiteral("Browse"), inputGroup);
    connect(browseButton, &QPushButton::clicked, this, &DiscImagePage::browseImage);
    m_inspectButton = new QPushButton(QStringLiteral("Inspect"), inputGroup);
    m_inspectButton->setObjectName("primary");
    connect(m_inspectButton, &QPushButton::clicked, this, &DiscImagePage::inspectImage);
    pathRow->addWidget(m_imageEdit, 1);
    pathRow->addWidget(browseButton);
    pathRow->addWidget(m_inspectButton);
    auto *pathWidget = new QWidget(inputGroup);
    pathWidget->setLayout(pathRow);
    form->addRow(QStringLiteral("Image"), pathWidget);

    m_formatLabel = new QLabel(QStringLiteral("Format: --"), inputGroup);
    form->addRow(m_formatLabel);
    m_mountLabel = new QLabel(QStringLiteral("Mount: not mounted"), inputGroup);
    m_mountLabel->setWordWrap(true);
    form->addRow(m_mountLabel);

    auto *actions = new QHBoxLayout();
    m_extractButton = new QPushButton(QStringLiteral("Extract Contents"), inputGroup);
    connect(m_extractButton, &QPushButton::clicked, this, &DiscImagePage::extractImage);
    m_mountButton = new QPushButton(QStringLiteral("Mount Read-Only"), inputGroup);
    connect(m_mountButton, &QPushButton::clicked, this, &DiscImagePage::mountImage);
    m_unmountButton = new QPushButton(QStringLiteral("Unmount"), inputGroup);
    connect(m_unmountButton, &QPushButton::clicked, this, &DiscImagePage::unmountImage);
    m_unmountButton->setEnabled(false);
    auto *createButton = new QPushButton(QStringLiteral("Create ISO"), inputGroup);
    connect(createButton, &QPushButton::clicked, this, &DiscImagePage::createIso);
    auto *checksumButton = new QPushButton(QStringLiteral("SHA-256"), inputGroup);
    connect(checksumButton, &QPushButton::clicked, this, &DiscImagePage::calculateChecksum);
    actions->addWidget(m_extractButton);
    actions->addWidget(m_mountButton);
    actions->addWidget(m_unmountButton);
    actions->addWidget(createButton);
    actions->addWidget(checksumButton);
    actions->addStretch();
    form->addRow(actions);

    auto *notice = new QLabel(
        QStringLiteral("Only ISO 9660/UDF images are mounted, read-only, through udisks2/polkit. "
                       "Other image types are inspected/extracted only where a backend supports "
                       "them. Format conversion by renaming is not implemented."),
        inputGroup);
    notice->setWordWrap(true);
    form->addRow(notice);
    layout->addWidget(inputGroup);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_details = new QTextEdit(splitter);
    m_details->setReadOnly(true);
    m_details->setMinimumWidth(300);
    m_treeView = new QTreeView(splitter);
    m_model = new eis::widgets::ArchiveTreeModel(this);
    m_treeView->setModel(m_model);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    splitter->addWidget(m_details);
    splitter->addWidget(m_treeView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    refreshMounts();
}

DiscImagePage::~DiscImagePage()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
    }
}

void DiscImagePage::browseImage()
{
    const QString start = m_imageEdit->text().isEmpty() ? AppSettings::instance().lastDirectory()
                                                        : QFileInfo(m_imageEdit->text()).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select disc image"), start,
        QStringLiteral("Disc images (*.iso *.img *.bin *.cue *.dmg *.udf *.raw);;All files (*)"));
    if (!file.isEmpty()) {
        m_imageEdit->setText(file);
        AppSettings::instance().setLastDirectory(QFileInfo(file).absolutePath());
    }
}

void DiscImagePage::inspectImage()
{
    const QString path = m_imageEdit->text().trimmed();
    if (path.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_info = m_engine.inspect(path, QString(), static_cast<int>(AppSettings::instance().maxEntries()));
    QApplication::restoreOverrideCursor();

    if (!m_info.ok) {
        m_details->setPlainText(QStringLiteral("Cannot inspect image.\n\n%1").arg(m_info.error));
        m_model->setListing(ArchiveListing());
        m_formatLabel->setText(QStringLiteral("Format: --"));
        refreshMounts();
        return;
    }

    m_model->setListing(m_info.listing);
    m_formatLabel->setText(QStringLiteral("Format: %1   ·   Size: %2   ·   Entries: %3")
                               .arg(m_info.format, utils::humanBytes(m_info.size))
                               .arg(m_info.listing.totalEntries));
    QString details;
    details += QStringLiteral("Path: %1\n").arg(m_info.path);
    details += QStringLiteral("Format: %1\n").arg(m_info.format);
    details += QStringLiteral("Backend: %1\n").arg(m_info.listing.backendName);
    details += QStringLiteral("Image size: %1\n").arg(utils::humanBytes(m_info.size));
    details += QStringLiteral("Entries: %1\n").arg(m_info.listing.totalEntries);
    details += QStringLiteral("Mountable: %1\n").arg(m_info.mountable ? QStringLiteral("yes (ISO/UDF)")
                                                                    : QStringLiteral("no"));
    details += QStringLiteral("xorriso: %1\n").arg(DiscImageEngine::canCreateIso()
                                                       ? DiscImageEngine::xorrisoVersion()
                                                       : QStringLiteral("not installed"));
    details += QStringLiteral("udisksctl: %1\n").arg(DiscImageEngine::canMount()
                                                         ? QStringLiteral("available")
                                                         : QStringLiteral("not installed"));
    if (!m_info.listing.error.isEmpty())
        details += QStringLiteral("\nWarning: %1\n").arg(m_info.listing.error);
    m_details->setPlainText(details);

    m_mountButton->setEnabled(m_info.mountable && DiscImageEngine::canMount());
    refreshMounts();
}

void DiscImagePage::extractImage()
{
    if (!m_info.ok && m_imageEdit->text().trimmed().isEmpty())
        return;
    const QString path = m_imageEdit->text().trimmed();
    if (path.isEmpty())
        return;
    const QString dest = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Extract image contents to"),
        QFileInfo(path).absolutePath());
    if (dest.isEmpty())
        return;

    ExtractionOptions options;
    options.archivePath = path;
    options.destination = dest;
    options.collision = CollisionPolicy::Rename;
    options.preservePermissions = AppSettings::instance().preservePermissions();
    options.maxEntries = AppSettings::instance().maxEntries();
    options.maxTotalBytes = AppSettings::instance().maxTotalBytes();
    const QString id = JobQueue::instance().enqueue(options);
    emit statusMessage(QStringLiteral("Image extraction queued (%1)").arg(id.left(8)));
}

void DiscImagePage::mountImage()
{
    const QString path = m_imageEdit->text().trimmed();
    if (path.isEmpty())
        return;
    const MountResult result = m_engine.mount(path);
    if (!result.ok) {
        QMessageBox::warning(this, QStringLiteral("Mount failed"), result.error);
        return;
    }
    m_mountLabel->setText(QStringLiteral("Mount: %1 at %2%3")
                              .arg(result.device, result.mountPoint,
                                   result.alreadyMounted ? QStringLiteral(" (already mounted)")
                                                         : QString()));
    m_unmountButton->setEnabled(true);
    emit statusMessage(QStringLiteral("Mounted read-only at %1").arg(result.mountPoint));
}

void DiscImagePage::unmountImage()
{
    const QString path = m_imageEdit->text().trimmed();
    QString error;
    if (!m_engine.unmount(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Unmount failed"), error);
        return;
    }
    m_mountLabel->setText(QStringLiteral("Mount: not mounted"));
    m_unmountButton->setEnabled(false);
    emit statusMessage(QStringLiteral("Image unmounted."));
}

void DiscImagePage::createIso()
{
    if (m_thread)
        return;
    const QString source = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select folder to build ISO from"), AppSettings::instance().lastDirectory());
    if (source.isEmpty())
        return;
    const QString output = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save ISO as"),
        source + QLatin1Char('/') + QFileInfo(source).fileName() + QStringLiteral(".iso"),
        QStringLiteral("ISO image (*.iso)"));
    if (output.isEmpty())
        return;
    bool ok = false;
    const QString label = QInputDialog::getText(
        this, QStringLiteral("Volume label"), QStringLiteral("ISO volume label:"),
        QLineEdit::Normal, QFileInfo(source).fileName().left(32).toUpper(), &ok);
    if (!ok)
        return;

    setBusy(true);
    m_thread = new QThread(this);
    auto *worker = new ImageWorker(ImageWorker::Mode::CreateIso, source, output, label);
    m_worker = worker;
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, worker, &ImageWorker::run);
    connect(worker, &ImageWorker::finished, this, &DiscImagePage::onWorkerFinished);
    connect(worker, &ImageWorker::finished, m_thread, &QThread::quit);
    QThread *thread = m_thread;
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread) {
            m_thread = nullptr;
            m_worker = nullptr;
        }
    });
    thread->start();
}

void DiscImagePage::calculateChecksum()
{
    if (m_thread)
        return;
    const QString path = m_imageEdit->text().trimmed();
    if (path.isEmpty())
        return;
    setBusy(true);
    m_thread = new QThread(this);
    auto *worker = new ImageWorker(ImageWorker::Mode::Hash, QString(), path, QString());
    m_worker = worker;
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, worker, &ImageWorker::run);
    connect(worker, &ImageWorker::finished, this, &DiscImagePage::onWorkerFinished);
    connect(worker, &ImageWorker::finished, m_thread, &QThread::quit);
    QThread *thread = m_thread;
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread) {
            m_thread = nullptr;
            m_worker = nullptr;
        }
    });
    thread->start();
}

void DiscImagePage::onWorkerFinished(bool ok, const QString &message)
{
    setBusy(false);
    m_details->setPlainText(message);
    emit statusMessage(ok ? QStringLiteral("Disc image operation completed")
                          : QStringLiteral("Disc image operation failed"));
    if (m_thread)
        m_thread->quit();
}

void DiscImagePage::refreshMounts()
{
    const QVector<MountRecord> mounts = m_engine.activeMounts();
    if (mounts.isEmpty()) {
        m_mountLabel->setText(QStringLiteral("Mount: not mounted"));
        m_unmountButton->setEnabled(false);
        return;
    }
    QStringList entries;
    for (const MountRecord &record : mounts) {
        entries << QStringLiteral("%1  ->  %2  (%3)")
                       .arg(record.imagePath, record.mountPoint, record.device);
        if (record.imagePath == QFileInfo(m_imageEdit->text().trimmed()).canonicalFilePath())
            m_unmountButton->setEnabled(true);
    }
    m_mountLabel->setText(QStringLiteral("Mount: %1").arg(entries.join(QStringLiteral(" | "))));
}

void DiscImagePage::setBusy(bool busy)
{
    m_inspectButton->setEnabled(!busy);
    m_extractButton->setEnabled(!busy);
    m_mountButton->setEnabled(!busy);
    m_unmountButton->setEnabled(!busy);
}

void DiscImagePage::closeEvent(QCloseEvent *event)
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
    }
    QWidget::closeEvent(event);
}

} // namespace eis::ui

#include "DiscImagePage.moc"
