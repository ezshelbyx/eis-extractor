#include "ui/OperationsPage.h"

#include "core/Types.h"
#include "settings/AppSettings.h"
#include "integrity/IntegrityChecker.h"
#include "core/Utils.h"
#include "queue/JobQueue.h"
#include "security/PathGuard.h"
#include "widgets/HudWidgets.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace eis::ui {

OperationsPage::OperationsPage(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Input group
    auto *inputGroup = new QGroupBox(QStringLiteral("EXTRACTION PARAMETERS"), this);
    auto *form = new QFormLayout(inputGroup);
    m_archiveEdit = new QLineEdit(inputGroup);
    auto *archiveRow = new QHBoxLayout();
    archiveRow->addWidget(m_archiveEdit);
    auto *browseArchiveBtn = new QPushButton(QStringLiteral("Browse"), inputGroup);
    connect(browseArchiveBtn, &QPushButton::clicked, this, &OperationsPage::browseArchive);
    archiveRow->addWidget(browseArchiveBtn);
    auto *archiveWidget = new QWidget(inputGroup);
    archiveWidget->setLayout(archiveRow);
    form->addRow(QStringLiteral("Archive"), archiveWidget);

    m_destEdit = new QLineEdit(inputGroup);
    auto *destRow = new QHBoxLayout();
    destRow->addWidget(m_destEdit);
    auto *browseDestBtn = new QPushButton(QStringLiteral("Browse"), inputGroup);
    connect(browseDestBtn, &QPushButton::clicked, this, &OperationsPage::browseDestination);
    destRow->addWidget(browseDestBtn);
    auto *destWidget = new QWidget(inputGroup);
    destWidget->setLayout(destRow);
    form->addRow(QStringLiteral("Destination"), destWidget);

    m_passwordEdit = new QLineEdit(inputGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password"), m_passwordEdit);

    m_collisionBox = new QComboBox(inputGroup);
    m_collisionBox->addItems({QStringLiteral("rename"), QStringLiteral("skip"), QStringLiteral("overwrite")});
    form->addRow(QStringLiteral("Collision"), m_collisionBox);

    m_preserveCheck = new QCheckBox(QStringLiteral("Preserve permissions"), inputGroup);
    m_preserveCheck->setChecked(true);
    form->addRow(m_preserveCheck);

    mainLayout->addWidget(inputGroup);

    // Action buttons
    auto *btnRow = new QHBoxLayout();
    auto *hereBtn = new QPushButton(QStringLiteral("Extract Here"), this);
    hereBtn->setObjectName("primary");
    connect(hereBtn, &QPushButton::clicked, this, &OperationsPage::extractHere);
    auto *newFolderBtn = new QPushButton(QStringLiteral("Extract to New Folder"), this);
    connect(newFolderBtn, &QPushButton::clicked, this, &OperationsPage::extractToNewFolder);
    auto *customBtn = new QPushButton(QStringLiteral("Extract to Custom Destination"), this);
    connect(customBtn, &QPushButton::clicked, this, &OperationsPage::extractToCustom);
    auto *queueBtn = new QPushButton(QStringLiteral("Queue"), this);
    connect(queueBtn, &QPushButton::clicked, this, &OperationsPage::queueJob);
    btnRow->addWidget(hereBtn);
    btnRow->addWidget(newFolderBtn);
    btnRow->addWidget(customBtn);
    btnRow->addWidget(queueBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // Progress panel
    auto *progressPanel = new QFrame(this);
    progressPanel->setObjectName("panel");
    auto *pl = new QVBoxLayout(progressPanel);
    m_progressLabel = new QLabel(QStringLiteral("Idle"), progressPanel);
    m_progressBar = new QProgressBar(progressPanel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_speedLabel = new QLabel(QStringLiteral("Speed: --"), progressPanel);
    m_etaLabel = new QLabel(QStringLiteral("ETA: --"), progressPanel);
    m_filesLabel = new QLabel(QStringLiteral("Files: 0/0"), progressPanel);
    pl->addWidget(m_progressLabel);
    pl->addWidget(m_progressBar);
    auto *statsRow = new QHBoxLayout();
    statsRow->addWidget(m_speedLabel);
    statsRow->addWidget(m_etaLabel);
    statsRow->addWidget(m_filesLabel);
    statsRow->addStretch();
    pl->addLayout(statsRow);
    mainLayout->addWidget(progressPanel);

    // Jobs table
    auto *jobsLabel = new QLabel(QStringLiteral("JOB QUEUE"), this);
    jobsLabel->setObjectName("section");
    mainLayout->addWidget(jobsLabel);

    m_jobsTable = new QTableWidget(this);
    m_jobsTable->setColumnCount(6);
    m_jobsTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Archive"), QStringLiteral("State"), QStringLiteral("Progress"), QStringLiteral("Files"), QStringLiteral("Backend")});
    m_jobsTable->horizontalHeader()->setStretchLastSection(true);
    m_jobsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_jobsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_jobsTable->setAlternatingRowColors(true);
    mainLayout->addWidget(m_jobsTable, 1);

    auto *jobBtnRow = new QHBoxLayout();
    auto *pauseBtn = new QPushButton(QStringLiteral("Pause"), this);
    connect(pauseBtn, &QPushButton::clicked, this, &OperationsPage::pauseSelected);
    auto *resumeBtn = new QPushButton(QStringLiteral("Resume"), this);
    connect(resumeBtn, &QPushButton::clicked, this, &OperationsPage::resumeSelected);
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    cancelBtn->setObjectName("danger");
    connect(cancelBtn, &QPushButton::clicked, this, &OperationsPage::cancelSelected);
    auto *retryBtn = new QPushButton(QStringLiteral("Retry"), this);
    connect(retryBtn, &QPushButton::clicked, this, &OperationsPage::retrySelected);
    auto *testBtn = new QPushButton(QStringLiteral("Test"), this);
    connect(testBtn, &QPushButton::clicked, this, &OperationsPage::testSelected);
    jobBtnRow->addWidget(pauseBtn);
    jobBtnRow->addWidget(resumeBtn);
    jobBtnRow->addWidget(cancelBtn);
    jobBtnRow->addWidget(retryBtn);
    jobBtnRow->addWidget(testBtn);
    jobBtnRow->addStretch();
    mainLayout->addLayout(jobBtnRow);

    m_collisionBox->setCurrentText(AppSettings::instance().collisionPolicy());
}

void OperationsPage::browseArchive()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select archive"), AppSettings::instance().lastDirectory(), utils::archiveNameFilter());
    if (!file.isEmpty()) {
        m_archiveEdit->setText(file);
        AppSettings::instance().setLastDirectory(QFileInfo(file).absolutePath());
    }
}

void OperationsPage::browseDestination()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select destination"), AppSettings::instance().lastDirectory());
    if (!dir.isEmpty()) {
        m_destEdit->setText(dir);
        AppSettings::instance().setLastDirectory(dir);
    }
}

eis::ExtractionOptions OperationsPage::buildOptions(bool toNewFolder, bool customDestination)
{
    ExtractionOptions opts;
    opts.archivePath = m_archiveEdit->text().trimmed();
    opts.password = m_passwordEdit->text();
    const QString policy = m_collisionBox->currentText();
    if (policy == QLatin1String("skip"))
        opts.collision = eis::CollisionPolicy::Skip;
    else if (policy == QLatin1String("overwrite"))
        opts.collision = eis::CollisionPolicy::Overwrite;
    else
        opts.collision = eis::CollisionPolicy::Rename;
    opts.preservePermissions = m_preserveCheck->isChecked();
    opts.maxEntries = AppSettings::instance().maxEntries();
    opts.maxTotalBytes = AppSettings::instance().maxTotalBytes();

    const QString archiveDir = QFileInfo(opts.archivePath).absolutePath();
    if (toNewFolder) {
        opts.destination = utils::uniqueDirectory(archiveDir + QLatin1Char('/') + utils::archiveBaseName(opts.archivePath));
    } else if (customDestination) {
        if (m_destEdit->text().trimmed().isEmpty())
            opts.destination = archiveDir;
        else
            opts.destination = m_destEdit->text().trimmed();
    } else {
        // Extract Here
        opts.destination = archiveDir;
    }
    return opts;
}

void OperationsPage::extractHere()
{
    enqueue(buildOptions(false, false));
}

void OperationsPage::extractToNewFolder()
{
    enqueue(buildOptions(true, false));
}

void OperationsPage::extractToCustom()
{
    enqueue(buildOptions(false, true));
}

void OperationsPage::queueJob()
{
    enqueue(buildOptions(false, true));
}

void OperationsPage::enqueue(const eis::ExtractionOptions &options)
{
    if (options.archivePath.isEmpty()) {
        emit statusMessage(QStringLiteral("No archive selected"));
        return;
    }
    if (!QFileInfo::exists(options.archivePath)) {
        emit statusMessage(QStringLiteral("Archive not found"));
        return;
    }
    AppSettings::instance().setCollisionPolicy(
        options.collision == eis::CollisionPolicy::Skip ? QStringLiteral("skip")
        : (options.collision == eis::CollisionPolicy::Overwrite ? QStringLiteral("overwrite") : QStringLiteral("rename")));
    const QString id = JobQueue::instance().enqueue(options);
    emit statusMessage(QStringLiteral("Queued job %1").arg(id));
    emit jobsChanged();
}

QString OperationsPage::selectedJobId() const
{
    const int row = m_jobsTable->currentRow();
    if (row < 0)
        return QString();
    auto *item = m_jobsTable->item(row, 0);
    return item ? item->text() : QString();
}

void OperationsPage::cancelSelected()
{
    const QString id = selectedJobId();
    if (!id.isEmpty()) {
        JobQueue::instance().cancel(id);
        emit jobsChanged();
    }
}

void OperationsPage::pauseSelected()
{
    const QString id = selectedJobId();
    if (!id.isEmpty()) {
        JobQueue::instance().pause(id);
        emit jobsChanged();
    }
}

void OperationsPage::resumeSelected()
{
    const QString id = selectedJobId();
    if (!id.isEmpty()) {
        JobQueue::instance().resume(id);
        emit jobsChanged();
    }
}

void OperationsPage::retrySelected()
{
    const QString id = selectedJobId();
    if (!id.isEmpty()) {
        JobQueue::instance().retry(id);
        emit jobsChanged();
    }
}

void OperationsPage::testSelected()
{
    const QString id = selectedJobId();
    if (id.isEmpty())
        return;
    const eis::JobRecord rec = JobQueue::instance().job(id);
    if (rec.options.archivePath.isEmpty())
        return;
    IntegrityChecker checker;
    const IntegrityResult res = checker.check(rec.options.archivePath, rec.options.password);
    if (res.ok)
        emit statusMessage(QStringLiteral("Integrity OK: %1").arg(rec.options.archivePath));
    else
        emit statusMessage(QStringLiteral("Integrity FAILED: %1").arg(res.error));
}

void OperationsPage::refreshJobs(const QVector<eis::JobRecord> &jobs)
{
    m_jobsTable->setRowCount(0);
    for (const JobRecord &rec : jobs) {
        const int row = m_jobsTable->rowCount();
        m_jobsTable->insertRow(row);
        m_jobsTable->setItem(row, 0, new QTableWidgetItem(rec.id.left(8)));
        m_jobsTable->setItem(row, 1, new QTableWidgetItem(QFileInfo(rec.options.archivePath).fileName()));
        m_jobsTable->setItem(row, 2, new QTableWidgetItem(jobStateLabel(rec.state)));
        auto *progressItem = new QTableWidgetItem(utils::percentString(rec.progress.bytesDone, rec.progress.bytesTotal));
        m_jobsTable->setItem(row, 3, progressItem);
        m_jobsTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1/%2").arg(rec.progress.filesDone).arg(rec.progress.filesTotal < 0 ? QStringLiteral("?") : QString::number(rec.progress.filesTotal))));
        m_jobsTable->setItem(row, 5, new QTableWidgetItem(rec.backendName));
    }
}

void OperationsPage::updateProgress(const QString &jobId, const eis::ProgressInfo &progress)
{
    for (int row = 0; row < m_jobsTable->rowCount(); ++row) {
        if (m_jobsTable->item(row, 0) && m_jobsTable->item(row, 0)->text() == jobId.left(8)) {
            m_jobsTable->item(row, 3)->setText(utils::percentString(progress.bytesDone, progress.bytesTotal));
            m_jobsTable->item(row, 4)->setText(QStringLiteral("%1/%2").arg(progress.filesDone).arg(progress.filesTotal < 0 ? QStringLiteral("?") : QString::number(progress.filesTotal)));
        }
    }
    m_progressLabel->setText(progress.currentFile.isEmpty() ? progress.status : progress.currentFile);
    if (progress.bytesTotal > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue((int)(100.0 * progress.bytesDone / progress.bytesTotal));
    } else {
        m_progressBar->setRange(0, 0);
    }
    m_speedLabel->setText(QStringLiteral("Speed: %1").arg(utils::humanSpeed(progress.bytesPerSecond)));
    m_etaLabel->setText(QStringLiteral("ETA: --"));
    m_filesLabel->setText(QStringLiteral("Files: %1/%2").arg(progress.filesDone).arg(progress.filesTotal < 0 ? QStringLiteral("?") : QString::number(progress.filesTotal)));
}

void OperationsPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void OperationsPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    m_archiveEdit->setText(urls.first().toLocalFile());
}

void OperationsPage::setArchivePath(const QString &path)
{
    if (m_archiveEdit)
        m_archiveEdit->setText(path);
    if (!path.isEmpty()) {
        QFileInfo fi(path);
        if (m_destEdit && m_destEdit->text().isEmpty())
            m_destEdit->setText(fi.absolutePath());
    }
}

} // namespace eis::ui

