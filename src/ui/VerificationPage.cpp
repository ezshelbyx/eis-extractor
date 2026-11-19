#include "ui/VerificationPage.h"

#include "core/Utils.h"
#include "settings/AppSettings.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>

namespace eis::ui {

namespace {

class HashWorker : public QObject {
    Q_OBJECT
public:
    enum class Mode { Hash, Generate, Verify };

    HashWorker(Mode mode, QStringList files, QString checksumPath, HashAlgorithm algorithm)
        : m_mode(mode)
        , m_files(std::move(files))
        , m_checksumPath(std::move(checksumPath))
        , m_algorithm(algorithm)
    {
    }

    void cancel() { m_cancel.store(true); }

public slots:
    void run()
    {
        QStringList lines;
        if (m_mode == Mode::Hash) {
            bool allOk = true;
            for (int i = 0; i < m_files.size(); ++i) {
                if (m_cancel.load()) {
                    emit finished(false, {QStringLiteral("Cancelled.")});
                    return;
                }
                emit progress(static_cast<int>(100.0 * i / qMax(1, m_files.size())),
                              QStringLiteral("Hashing %1").arg(QFileInfo(m_files[i]).fileName()));
                const HashResult result = HashEngine::hashFile(
                    m_files[i], m_algorithm, &m_cancel,
                    [this, i](qint64 done, qint64 total) {
                        if (total > 0) {
                            emit progress(static_cast<int>(100.0 * done / total),
                                          QStringLiteral("Hashing %1")
                                              .arg(QFileInfo(m_files[i]).fileName()));
                        }
                    });
                if (!result.ok) {
                    allOk = false;
                    lines << QStringLiteral("ERROR  %1  %2").arg(result.path, result.error);
                } else {
                    lines << QStringLiteral("%1  %2").arg(result.digest, result.path);
                }
            }
            emit finished(allOk, lines);
            return;
        }

        if (m_mode == Mode::Generate) {
            QString error;
            const bool ok = HashEngine::generateChecksumFile(
                m_files, m_checksumPath, m_algorithm, &error, &m_cancel);
            if (ok) {
                emit finished(true, {QStringLiteral("Checksum file written: %1").arg(m_checksumPath)});
            } else {
                emit finished(false, {error});
            }
            return;
        }

        emit progress(0, QStringLiteral("Verifying checksums..."));
        const ChecksumFileResult result = HashEngine::verifyChecksumFile(m_checksumPath, &m_cancel);
        lines << QStringLiteral("Checksum file: %1").arg(result.checksumFile);
        lines << QStringLiteral("Algorithm: %1").arg(result.algorithm);
        lines << QStringLiteral("Checked: %1  matched: %2  mismatched: %3  missing: %4")
                     .arg(result.checked).arg(result.matched)
                     .arg(result.mismatched).arg(result.missing);
        for (const QString &error : result.errors)
            lines << QStringLiteral("ERROR  %1").arg(error);
        for (const QString &mismatch : result.mismatches)
            lines << QStringLiteral("MISMATCH  %1").arg(mismatch);
        if (result.ok)
            lines << QStringLiteral("All checksums match.");
        else
            lines << QStringLiteral("Verification FAILED.");
        emit finished(result.ok, lines);
    }

signals:
    void progress(int percent, QString status);
    void finished(bool ok, QStringList lines);

private:
    Mode m_mode;
    QStringList m_files;
    QString m_checksumPath;
    HashAlgorithm m_algorithm;
    std::atomic<bool> m_cancel{false};
};

} // namespace

VerificationPage::VerificationPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("FILE VERIFICATION"), this);
    title->setObjectName("title");
    layout->addWidget(title);

    auto *hashGroup = new QGroupBox(QStringLiteral("HASH FILES"), this);
    auto *hashLayout = new QVBoxLayout(hashGroup);
    m_files = new QListWidget(hashGroup);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    hashLayout->addWidget(m_files, 1);

    auto *addRow = new QHBoxLayout();
    auto *addButton = new QPushButton(QStringLiteral("Add Files"), hashGroup);
    connect(addButton, &QPushButton::clicked, this, &VerificationPage::addFiles);
    auto *removeButton = new QPushButton(QStringLiteral("Remove"), hashGroup);
    connect(removeButton, &QPushButton::clicked, this, &VerificationPage::removeSelected);
    m_algorithm = new QComboBox(hashGroup);
    for (const QString &name : hashAlgorithmNames())
        m_algorithm->addItem(name);
    m_hashButton = new QPushButton(QStringLiteral("Calculate"), hashGroup);
    m_hashButton->setObjectName("primary");
    connect(m_hashButton, &QPushButton::clicked, this, &VerificationPage::calculateHashes);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), hashGroup);
    m_cancelButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &VerificationPage::cancelOperation);
    addRow->addWidget(addButton);
    addRow->addWidget(removeButton);
    addRow->addWidget(m_algorithm);
    addRow->addWidget(m_hashButton);
    addRow->addWidget(m_cancelButton);
    addRow->addStretch();
    hashLayout->addLayout(addRow);
    layout->addWidget(hashGroup, 1);

    auto *checksumGroup = new QGroupBox(QStringLiteral("CHECKSUM FILES"), this);
    auto *checksumForm = new QFormLayout(checksumGroup);

    auto *generateRow = new QHBoxLayout();
    m_checksumOutputEdit = new QLineEdit(checksumGroup);
    m_checksumOutputEdit->setPlaceholderText(QStringLiteral("checksums.sha256"));
    auto *browseOutputButton = new QPushButton(QStringLiteral("Browse"), checksumGroup);
    connect(browseOutputButton, &QPushButton::clicked,
            this, &VerificationPage::browseChecksumOutput);
    m_generateButton = new QPushButton(QStringLiteral("Generate"), checksumGroup);
    connect(m_generateButton, &QPushButton::clicked,
            this, &VerificationPage::generateChecksumFile);
    generateRow->addWidget(m_checksumOutputEdit, 1);
    generateRow->addWidget(browseOutputButton);
    generateRow->addWidget(m_generateButton);
    auto *generateWidget = new QWidget(checksumGroup);
    generateWidget->setLayout(generateRow);
    checksumForm->addRow(QStringLiteral("Write checksum file"), generateWidget);

    auto *verifyRow = new QHBoxLayout();
    m_checksumFileEdit = new QLineEdit(checksumGroup);
    m_checksumFileEdit->setPlaceholderText(QStringLiteral("checksum file to verify"));
    auto *browseChecksumButton = new QPushButton(QStringLiteral("Browse"), checksumGroup);
    connect(browseChecksumButton, &QPushButton::clicked,
            this, &VerificationPage::browseChecksumFile);
    m_verifyButton = new QPushButton(QStringLiteral("Verify"), checksumGroup);
    connect(m_verifyButton, &QPushButton::clicked, this, &VerificationPage::verifyChecksumFile);
    verifyRow->addWidget(m_checksumFileEdit, 1);
    verifyRow->addWidget(browseChecksumButton);
    verifyRow->addWidget(m_verifyButton);
    auto *verifyWidget = new QWidget(checksumGroup);
    verifyWidget->setLayout(verifyRow);
    checksumForm->addRow(QStringLiteral("Verify checksum file"), verifyWidget);

    auto *notice = new QLabel(
        QStringLiteral("SHA-1 and MD5 are provided for legacy compatibility only. A matching "
                       "checksum proves integrity, not authenticity: it does not prove who "
                       "published the file."),
        checksumGroup);
    notice->setWordWrap(true);
    checksumForm->addRow(notice);
    layout->addWidget(checksumGroup);

    m_status = new QLabel(QStringLiteral("Idle"), this);
    layout->addWidget(m_status);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_output = new QTextEdit(this);
    m_output->setReadOnly(true);
    layout->addWidget(m_output, 1);
}

VerificationPage::~VerificationPage()
{
    if (m_thread) {
        if (m_worker)
            static_cast<HashWorker *>(m_worker)->cancel();
        m_thread->quit();
        m_thread->wait(5000);
    }
}

void VerificationPage::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Select files to hash"),
        AppSettings::instance().lastDirectory(), QStringLiteral("All files (*)"));
    for (const QString &file : files) {
        bool exists = false;
        for (int i = 0; i < m_files->count(); ++i) {
            if (m_files->item(i)->data(Qt::UserRole).toString() == file)
                exists = true;
        }
        if (exists)
            continue;
        auto *item = new QListWidgetItem(file, m_files);
        item->setData(Qt::UserRole, file);
    }
    if (!files.isEmpty())
        AppSettings::instance().setLastDirectory(QFileInfo(files.first()).absolutePath());
    if (m_checksumOutputEdit->text().isEmpty() && !files.isEmpty()) {
        m_checksumOutputEdit->setText(QFileInfo(files.first()).absolutePath()
                                      + QStringLiteral("/checksums.sha256"));
    }
}

void VerificationPage::removeSelected()
{
    for (QListWidgetItem *item : m_files->selectedItems())
        delete item;
}

void VerificationPage::calculateHashes()
{
    if (m_thread || m_files->count() == 0)
        return;
    startWorker(Mode::Hash);
}

void VerificationPage::browseChecksumOutput()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Write checksum file"),
        m_checksumOutputEdit->text().isEmpty() ? AppSettings::instance().lastDirectory()
                                               : m_checksumOutputEdit->text(),
        QStringLiteral("Checksum files (*.sha256 *.sha512 *.sha1 *.md5 *.txt);;All files (*)"));
    if (!file.isEmpty())
        m_checksumOutputEdit->setText(file);
}

void VerificationPage::generateChecksumFile()
{
    if (m_thread || m_files->count() == 0)
        return;
    if (m_checksumOutputEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Checksum"),
                                 QStringLiteral("Choose an output checksum file."));
        return;
    }
    startWorker(Mode::Generate);
}

void VerificationPage::browseChecksumFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select checksum file"), AppSettings::instance().lastDirectory(),
        QStringLiteral("Checksum files (*.sha256 *.sha512 *.sha1 *.md5 *.txt);;All files (*)"));
    if (!file.isEmpty())
        m_checksumFileEdit->setText(file);
}

void VerificationPage::verifyChecksumFile()
{
    if (m_thread)
        return;
    if (m_checksumFileEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Verify"),
                                 QStringLiteral("Choose a checksum file."));
        return;
    }
    startWorker(Mode::Verify);
}

void VerificationPage::startWorker(Mode mode)
{
    QStringList files;
    for (int i = 0; i < m_files->count(); ++i)
        files << m_files->item(i)->data(Qt::UserRole).toString();

    bool ok = false;
    const HashAlgorithm algorithm = hashAlgorithmFromName(m_algorithm->currentText(), &ok);
    if (!ok)
        return;

    const QString checksumPath = mode == Mode::Generate ? m_checksumOutputEdit->text().trimmed()
                                                        : m_checksumFileEdit->text().trimmed();

    m_output->clear();
    setBusy(true);

    m_thread = new QThread(this);
    auto *worker = new HashWorker(static_cast<HashWorker::Mode>(mode), files, checksumPath, algorithm);
    m_worker = worker;
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, worker, &HashWorker::run);
    connect(worker, &HashWorker::progress, this, &VerificationPage::onProgress);
    connect(worker, &HashWorker::finished, this, &VerificationPage::onFinished);
    connect(worker, &HashWorker::finished, m_thread, &QThread::quit);
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

void VerificationPage::cancelOperation()
{
    if (m_worker)
        static_cast<HashWorker *>(m_worker)->cancel();
    m_status->setText(QStringLiteral("Cancelling..."));
}

void VerificationPage::onProgress(int percent, const QString &status)
{
    m_progress->setRange(0, 100);
    m_progress->setValue(qBound(0, percent, 100));
    m_status->setText(status);
}

void VerificationPage::onFinished(bool ok, const QStringList &lines)
{
    setBusy(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(ok ? 100 : 0);
    m_status->setText(ok ? QStringLiteral("Completed") : QStringLiteral("Failed or incomplete"));
    m_output->setPlainText(lines.join(QLatin1Char('\n')));
    if (m_thread)
        m_thread->quit();
    emit statusMessage(ok ? QStringLiteral("Verification completed")
                          : QStringLiteral("Verification did not complete successfully"));
}

void VerificationPage::setBusy(bool busy)
{
    m_hashButton->setEnabled(!busy);
    m_generateButton->setEnabled(!busy);
    m_verifyButton->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    m_files->setEnabled(!busy);
}

void VerificationPage::closeEvent(QCloseEvent *event)
{
    if (m_thread) {
        if (m_worker)
            static_cast<HashWorker *>(m_worker)->cancel();
        m_thread->quit();
        m_thread->wait(5000);
    }
    QWidget::closeEvent(event);
}

} // namespace eis::ui

#include "VerificationPage.moc"
