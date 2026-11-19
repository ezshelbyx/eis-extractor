#include "ui/CompressionPage.h"

#include "core/Capabilities.h"
#include "core/Utils.h"
#include "settings/AppSettings.h"
#include "vault/PasswordVault.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

namespace eis::ui {

namespace {

class CompressionWorker : public QObject {
    Q_OBJECT
public:
    explicit CompressionWorker(CompressionRequest request)
        : m_request(std::move(request)) {}

    void cancel() { m_control.cancel.store(true); }

public slots:
    void run()
    {
        const CompressionResult result = CompressionEngine::create(
            m_request, &m_control, [this](const ProgressInfo &p) { emit progress(p); });
        emit finished(result);
    }

signals:
    void progress(eis::ProgressInfo progress);
    void finished(eis::CompressionResult result);

private:
    CompressionRequest m_request;
    JobControl m_control;
};

} // namespace

CompressionPage::CompressionPage(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("COMPRESSION ENGINE"), this);
    title->setObjectName("title");
    layout->addWidget(title);

    auto *inputGroup = new QGroupBox(QStringLiteral("SOURCES"), this);
    auto *inputLayout = new QVBoxLayout(inputGroup);
    m_sources = new QListWidget(inputGroup);
    m_sources->setSelectionMode(QAbstractItemView::ExtendedSelection);
    inputLayout->addWidget(m_sources, 1);

    auto *sourceButtons = new QHBoxLayout();
    auto *addFilesButton = new QPushButton(QStringLiteral("Add Files"), inputGroup);
    auto *addFolderButton = new QPushButton(QStringLiteral("Add Folder"), inputGroup);
    auto *removeButton = new QPushButton(QStringLiteral("Remove"), inputGroup);
    connect(addFilesButton, &QPushButton::clicked, this, &CompressionPage::addFiles);
    connect(addFolderButton, &QPushButton::clicked, this, &CompressionPage::addFolder);
    connect(removeButton, &QPushButton::clicked, this, &CompressionPage::removeSelected);
    sourceButtons->addWidget(addFilesButton);
    sourceButtons->addWidget(addFolderButton);
    sourceButtons->addWidget(removeButton);
    sourceButtons->addStretch();
    inputLayout->addLayout(sourceButtons);
    layout->addWidget(inputGroup, 1);

    auto *optionsGroup = new QGroupBox(QStringLiteral("ARCHIVE"), this);
    auto *form = new QFormLayout(optionsGroup);

    auto *outputRow = new QHBoxLayout();
    m_outputEdit = new QLineEdit(optionsGroup);
    m_outputEdit->setPlaceholderText(QStringLiteral("output archive path"));
    connect(m_outputEdit, &QLineEdit::textEdited, this, [this]() { m_autoOutput = false; });
    auto *browseOutputButton = new QPushButton(QStringLiteral("Browse"), optionsGroup);
    connect(browseOutputButton, &QPushButton::clicked, this, &CompressionPage::browseOutput);
    outputRow->addWidget(m_outputEdit);
    outputRow->addWidget(browseOutputButton);
    auto *outputWidget = new QWidget(optionsGroup);
    outputWidget->setLayout(outputRow);
    form->addRow(QStringLiteral("Output"), outputWidget);

    m_formatBox = new QComboBox(optionsGroup);
    for (const QString &id : CapabilityRegistry::instance().creatableFormats()) {
        const FormatCapability capability = CapabilityRegistry::instance().format(id);
        m_formatBox->addItem(QStringLiteral("%1 (%2)").arg(capability.label, id), id);
    }
    connect(m_formatBox, &QComboBox::currentIndexChanged, this, &CompressionPage::formatChanged);
    form->addRow(QStringLiteral("Format"), m_formatBox);

    m_levelBox = new QComboBox(optionsGroup);
    m_levelBox->addItem(QStringLiteral("Store"), static_cast<int>(CompressionLevel::Store));
    m_levelBox->addItem(QStringLiteral("Fast"), static_cast<int>(CompressionLevel::Fast));
    m_levelBox->addItem(QStringLiteral("Normal"), static_cast<int>(CompressionLevel::Normal));
    m_levelBox->addItem(QStringLiteral("Maximum"), static_cast<int>(CompressionLevel::Maximum));
    m_levelBox->setCurrentIndex(2);
    form->addRow(QStringLiteral("Compression level"), m_levelBox);

    m_passwordEdit = new QLineEdit(optionsGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("optional archive password"));
    form->addRow(QStringLiteral("Password"), m_passwordEdit);

    m_encryptHeadersCheck = new QCheckBox(QStringLiteral("Encrypt file names (7z only)"), optionsGroup);
    m_encryptHeadersCheck->setChecked(true);
    form->addRow(m_encryptHeadersCheck);

    m_volumeSpin = new QSpinBox(optionsGroup);
    m_volumeSpin->setRange(0, 1024 * 100);
    m_volumeSpin->setSuffix(QStringLiteral(" MiB"));
    m_volumeSpin->setSpecialValueText(QStringLiteral("single file"));
    form->addRow(QStringLiteral("Volume size"), m_volumeSpin);

    m_overwriteCheck = new QCheckBox(QStringLiteral("Replace existing archive"), optionsGroup);
    form->addRow(m_overwriteCheck);

    layout->addWidget(optionsGroup);

    auto *buttonRow = new QHBoxLayout();
    m_createButton = new QPushButton(QStringLiteral("Create Archive"), this);
    m_createButton->setObjectName("primary");
    connect(m_createButton, &QPushButton::clicked, this, &CompressionPage::createArchive);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &CompressionPage::cancelOperation);
    buttonRow->addWidget(m_createButton);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto *progressGroup = new QGroupBox(QStringLiteral("PROGRESS"), this);
    auto *progressLayout = new QVBoxLayout(progressGroup);
    m_currentLabel = new QLabel(QStringLiteral("Idle"), progressGroup);
    m_progressBar = new QProgressBar(progressGroup);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    progressLayout->addWidget(m_currentLabel);
    progressLayout->addWidget(m_progressBar);
    auto *statsRow = new QHBoxLayout();
    m_speedLabel = new QLabel(QStringLiteral("Speed: --"), progressGroup);
    m_etaLabel = new QLabel(QStringLiteral("Remaining: --"), progressGroup);
    m_ratioLabel = new QLabel(QStringLiteral("Ratio: --"), progressGroup);
    statsRow->addWidget(m_speedLabel);
    statsRow->addWidget(m_etaLabel);
    statsRow->addWidget(m_ratioLabel);
    statsRow->addStretch();
    progressLayout->addLayout(statsRow);
    layout->addWidget(progressGroup);

    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(120);
    layout->addWidget(m_log);

    updateFormatCapabilities();
    formatChanged();
}

CompressionPage::~CompressionPage()
{
    if (m_thread) {
        if (m_worker)
            static_cast<CompressionWorker *>(m_worker)->cancel();
        m_thread->quit();
        m_thread->wait(5000);
    }
}

void CompressionPage::setSources(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        bool exists = false;
        for (int i = 0; i < m_sources->count(); ++i) {
            if (m_sources->item(i)->data(Qt::UserRole).toString() == path)
                exists = true;
        }
        if (exists)
            continue;
        auto *item = new QListWidgetItem(QFileInfo(path).fileName() + QStringLiteral("  —  ") + path,
                                         m_sources);
        item->setData(Qt::UserRole, path);
    }
    if (m_autoOutput || m_outputEdit->text().isEmpty()) {
        QStringList sources;
        for (int i = 0; i < m_sources->count(); ++i)
            sources << m_sources->item(i)->data(Qt::UserRole).toString();
        const QString format = m_formatBox->currentData().toString();
        if (!sources.isEmpty()) {
            m_outputEdit->setText(CompressionEngine::suggestOutputPath(sources, format));
            m_autoOutput = true;
        }
    }
}

void CompressionPage::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Select files to compress"),
        AppSettings::instance().lastDirectory(), QStringLiteral("All files (*)"));
    if (!files.isEmpty()) {
        AppSettings::instance().setLastDirectory(QFileInfo(files.first()).absolutePath());
        setSources(files);
    }
}

void CompressionPage::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select folder to compress"), AppSettings::instance().lastDirectory());
    if (!folder.isEmpty()) {
        AppSettings::instance().setLastDirectory(folder);
        setSources({folder});
    }
}

void CompressionPage::removeSelected()
{
    const QList<QListWidgetItem *> selected = m_sources->selectedItems();
    for (QListWidgetItem *item : selected)
        delete item;
    if (m_autoOutput) {
        QStringList sources;
        for (int i = 0; i < m_sources->count(); ++i)
            sources << m_sources->item(i)->data(Qt::UserRole).toString();
        if (!sources.isEmpty())
            m_outputEdit->setText(
                CompressionEngine::suggestOutputPath(sources, m_formatBox->currentData().toString()));
        else
            m_outputEdit->clear();
    }
}

void CompressionPage::browseOutput()
{
    const QString format = m_formatBox->currentData().toString();
    const FormatCapability capability = CapabilityRegistry::instance().format(format);
    const QString suffix = capability.extensions.isEmpty() ? format : capability.extensions.first();
    const QString start = m_outputEdit->text().isEmpty()
                              ? AppSettings::instance().lastDirectory()
                              : m_outputEdit->text();
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save archive as"), start,
        QStringLiteral("%1 (*.%2);;All files (*)").arg(capability.label, suffix));
    if (!file.isEmpty()) {
        m_outputEdit->setText(file);
        m_autoOutput = false;
    }
}

void CompressionPage::formatChanged()
{
    updateFormatCapabilities();
    if (m_autoOutput && !m_outputEdit->text().isEmpty()) {
        QStringList sources;
        for (int i = 0; i < m_sources->count(); ++i)
            sources << m_sources->item(i)->data(Qt::UserRole).toString();
        if (!sources.isEmpty()) {
            m_outputEdit->setText(
                CompressionEngine::suggestOutputPath(sources, m_formatBox->currentData().toString()));
        }
    }
}

void CompressionPage::updateFormatCapabilities()
{
    const QString format = m_formatBox->currentData().toString();
    const FormatCapability capability = CapabilityRegistry::instance().format(format);
    m_passwordEdit->setEnabled(capability.canEncrypt);
    if (!capability.canEncrypt) {
        m_passwordEdit->setPlaceholderText(
            QStringLiteral("encryption is not supported for %1").arg(capability.label));
    } else {
        m_passwordEdit->setPlaceholderText(QStringLiteral("optional archive password"));
    }
    m_encryptHeadersCheck->setEnabled(capability.canEncrypt && format == QLatin1String("7z"));
    m_volumeSpin->setEnabled(capability.canMultiVolume);
    if (!capability.canMultiVolume)
        m_volumeSpin->setValue(0);
}

CompressionRequest CompressionPage::buildRequest() const
{
    CompressionRequest request;
    for (int i = 0; i < m_sources->count(); ++i)
        request.sources << m_sources->item(i)->data(Qt::UserRole).toString();
    request.outputPath = m_outputEdit->text().trimmed();
    request.format = m_formatBox->currentData().toString();
    request.level = static_cast<CompressionLevel>(m_levelBox->currentData().toInt());
    request.password = m_passwordEdit->text();
    request.encryptHeaders = m_encryptHeadersCheck->isChecked();
    request.volumeSize = static_cast<qint64>(m_volumeSpin->value()) * 1024 * 1024;
    request.overwrite = m_overwriteCheck->isChecked();
    return request;
}

void CompressionPage::createArchive()
{
    if (m_thread)
        return;

    CompressionRequest request = buildRequest();
    QString error;
    if (!CompressionEngine::validate(request, &error)) {
        QMessageBox::warning(this, QStringLiteral("Compression"), error);
        return;
    }
    if (QFileInfo::exists(request.outputPath) && !request.overwrite) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Replace archive"),
            QStringLiteral("%1 already exists. Replace it?").arg(request.outputPath));
        if (answer != QMessageBox::Yes)
            return;
        request.overwrite = true;
    }

    setBusy(true);
    m_log->clear();
    appendLog(QStringLiteral("Creating %1 archive: %2")
                  .arg(request.format, request.outputPath));
    if (!request.password.isEmpty())
        appendLog(QStringLiteral("Encryption enabled."));

    m_thread = new QThread(this);
    auto *worker = new CompressionWorker(request);
    m_worker = worker;
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, worker, &CompressionWorker::run);
    connect(worker, &CompressionWorker::progress, this, &CompressionPage::onProgress);
    connect(worker, &CompressionWorker::finished, this, &CompressionPage::onFinished);
    QThread *thread = m_thread;
    connect(worker, &CompressionWorker::finished, thread, &QThread::quit);
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

void CompressionPage::cancelOperation()
{
    if (!m_thread || !m_worker)
        return;
    static_cast<CompressionWorker *>(m_worker)->cancel();
    m_currentLabel->setText(QStringLiteral("Cancelling..."));
}

void CompressionPage::onProgress(const eis::ProgressInfo &progress)
{
    m_currentLabel->setText(progress.currentFile.isEmpty() ? progress.status
                                                           : progress.currentFile);
    if (progress.bytesTotal > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>(100.0 * progress.bytesDone
                                                 / static_cast<double>(progress.bytesTotal)));
    } else {
        m_progressBar->setRange(0, 0);
    }
    m_speedLabel->setText(QStringLiteral("Speed: %1").arg(utils::humanSpeed(progress.bytesPerSecond)));
    if (progress.bytesPerSecond > 0 && progress.bytesTotal > 0 && progress.bytesDone >= 0) {
        const double remaining = (progress.bytesTotal - progress.bytesDone) / progress.bytesPerSecond;
        m_etaLabel->setText(QStringLiteral("Remaining: %1").arg(utils::humanDuration(
            static_cast<qint64>(remaining * 1000.0))));
    } else {
        m_etaLabel->setText(QStringLiteral("Remaining: --"));
    }
    if (progress.compressionRatio > 0) {
        m_ratioLabel->setText(QStringLiteral("Ratio: %1x (%2)")
                                  .arg(progress.compressionRatio, 0, 'f', 2)
                                  .arg(utils::humanBytes(progress.outputBytes)));
    } else {
        m_ratioLabel->setText(QStringLiteral("Ratio: --"));
    }
}

void CompressionPage::onFinished(const eis::CompressionResult &result)
{
    setBusy(false);
    if (result.ok) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_currentLabel->setText(QStringLiteral("Completed"));
        m_ratioLabel->setText(QStringLiteral("Ratio: %1  —  %2")
                                  .arg(utils::humanBytes(result.inputBytes),
                                       utils::humanBytes(result.outputBytes)));
        appendLog(QStringLiteral("Completed: %1 files, %2 in, %3 out, %4 sec (%5)")
                      .arg(result.filesAdded)
                      .arg(utils::humanBytes(result.inputBytes),
                           utils::humanBytes(result.outputBytes))
                      .arg(result.durationMs / 1000.0, 0, 'f', 1)
                      .arg(result.backend));
        for (const QString &warning : result.warnings)
            appendLog(QStringLiteral("Warning: %1").arg(warning));
        emit statusMessage(QStringLiteral("Archive created: %1").arg(result.outputPath));

        // Offer to store the password in the unlocked vault without logging it.
        if (result.encrypted && !m_passwordEdit->text().isEmpty()
            && PasswordVault::instance().isUnlocked()) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Save password"),
                QStringLiteral("Store this archive password in the unlocked E.I.S vault?"));
            if (answer == QMessageBox::Yes) {
                ArchivePasswordRecord record;
                record.archivePath = result.outputPath;
                record.archiveName = QFileInfo(result.outputPath).fileName();
                record.password = m_passwordEdit->text();
                QString vaultError;
                if (!PasswordVault::instance().addArchivePassword(record, &vaultError)
                    || !PasswordVault::instance().save(&vaultError)) {
                    QMessageBox::warning(this, QStringLiteral("Vault"), vaultError);
                }
            }
        }
    } else {
        m_currentLabel->setText(result.cancelled ? QStringLiteral("Cancelled")
                                                 : QStringLiteral("Failed"));
        appendLog(QStringLiteral("Failed: %1").arg(result.error));
        emit statusMessage(QStringLiteral("Compression failed: %1").arg(result.error));
    }

    if (m_thread)
        m_thread->quit();
}

void CompressionPage::setBusy(bool busy)
{
    m_createButton->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    m_sources->setEnabled(!busy);
    m_outputEdit->setEnabled(!busy);
    m_formatBox->setEnabled(!busy);
    if (busy) {
        m_progressBar->setRange(0, 0);
        m_progressBar->setValue(0);
        m_speedLabel->setText(QStringLiteral("Speed: --"));
        m_etaLabel->setText(QStringLiteral("Remaining: --"));
        m_ratioLabel->setText(QStringLiteral("Ratio: --"));
    }
}

void CompressionPage::appendLog(const QString &text)
{
    m_log->append(text);
}

void CompressionPage::closeEvent(QCloseEvent *event)
{
    if (m_thread) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Compression running"),
            QStringLiteral("Cancel the running compression and close?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        static_cast<CompressionWorker *>(m_worker)->cancel();
        m_thread->quit();
        m_thread->wait(5000);
    }
    QWidget::closeEvent(event);
}

void CompressionPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void CompressionPage::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    setSources(paths);
}

} // namespace eis::ui

#include "CompressionPage.moc"
