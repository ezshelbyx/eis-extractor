#include "ui/InspectorPage.h"

#include "archive/ArchiveService.h"
#include "core/Utils.h"
#include "queue/JobQueue.h"
#include "security/PathGuard.h"
#include "settings/AppSettings.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QSplitter>
#include <QPushButton>
#include <QTextEdit>
#include <QTreeView>
#include <QVBoxLayout>

namespace eis::ui {

InspectorPage::InspectorPage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *inputGroup = new QGroupBox(QStringLiteral("ARCHIVE INSPECTOR"), this);
    auto *form = new QFormLayout(inputGroup);
    m_archiveEdit = new QLineEdit(inputGroup);
    auto *archiveRow = new QHBoxLayout();
    archiveRow->addWidget(m_archiveEdit);
    auto *browseBtn = new QPushButton(QStringLiteral("Browse"), inputGroup);
    connect(browseBtn, &QPushButton::clicked, this, &InspectorPage::browseArchive);
    archiveRow->addWidget(browseBtn);
    auto *aw = new QWidget(inputGroup);
    aw->setLayout(archiveRow);
    form->addRow(QStringLiteral("Archive"), aw);

    m_passwordEdit = new QLineEdit(inputGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password"), m_passwordEdit);

    auto *loadBtn = new QPushButton(QStringLiteral("Load Listing"), inputGroup);
    loadBtn->setObjectName("primary");
    connect(loadBtn, &QPushButton::clicked, this, &InspectorPage::loadArchive);
    form->addRow(loadBtn);
    mainLayout->addWidget(inputGroup);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_treeView = new QTreeView(splitter);
    m_model = new eis::widgets::ArchiveTreeModel(this);
    m_treeView->setModel(m_model);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->header()->setStretchLastSection(true);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (current.isValid())
            showDetails(current.row());
    });

    m_detailsEdit = new QTextEdit(splitter);
    m_detailsEdit->setReadOnly(true);
    splitter->addWidget(m_treeView);
    splitter->addWidget(m_detailsEdit);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    auto *btnRow = new QHBoxLayout();
    auto *extractBtn = new QPushButton(QStringLiteral("Extract Selected"), this);
    connect(extractBtn, &QPushButton::clicked, this, &InspectorPage::extractSelected);
    btnRow->addWidget(extractBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);
}

void InspectorPage::browseArchive()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select archive"), AppSettings::instance().lastDirectory(), utils::archiveNameFilter());
    if (!file.isEmpty()) {
        m_archiveEdit->setText(file);
        AppSettings::instance().setLastDirectory(QFileInfo(file).absolutePath());
    }
}

void InspectorPage::loadArchive()
{
    const QString path = m_archiveEdit->text().trimmed();
    if (path.isEmpty())
        return;
    m_progressBar->setVisible(true);
    ArchiveService service;
    m_listing = service.list(path, m_passwordEdit->text(), (int)AppSettings::instance().maxEntries());
    m_model->setListing(m_listing);
    m_progressBar->setVisible(false);

    if (!m_listing.ok) {
        QMessageBox::warning(this, QStringLiteral("Listing failed"), m_listing.error);
        return;
    }
    m_detailsEdit->setPlainText(QStringLiteral("Format: %1\nBackend: %2\nEntries: %3\nEncrypted: %4\nArchive size: %5")
                                    .arg(m_listing.formatName, m_listing.backendName)
                                    .arg(m_listing.totalEntries)
                                    .arg(m_listing.encryptedCount)
                                    .arg(utils::humanBytes(m_listing.archiveSize)));
}

void InspectorPage::showDetails(int row)
{
    const ArchiveEntryInfo e = m_model->entryAt(row);
    if (e.path.isEmpty())
        return;
    m_detailsEdit->setPlainText(QStringLiteral("Path: %1\nSize: %2\nPacked: %3\nType: %4\nEncrypted: %5\nModified: %6\nLink target: %7")
                                    .arg(e.path)
                                    .arg(e.size >= 0 ? utils::humanBytes(e.size) : QStringLiteral("--"))
                                    .arg(e.compressedSize >= 0 ? utils::humanBytes(e.compressedSize) : QStringLiteral("--"))
                                    .arg(e.isDir ? QStringLiteral("directory") : (e.isSymlink ? QStringLiteral("symlink") : e.typeName))
                                    .arg(e.encrypted ? QStringLiteral("yes") : QStringLiteral("no"))
                                    .arg(e.modified.isValid() ? e.modified.toString(Qt::ISODate) : QStringLiteral("--"))
                                    .arg(e.linkTarget));
}

void InspectorPage::extractSelected()
{
    const QModelIndex idx = m_treeView->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::information(this, QStringLiteral("Extract"), QStringLiteral("Select an entry first."));
        return;
    }
    const ArchiveEntryInfo e = m_model->entryAt(idx.row());
    if (e.path.isEmpty())
        return;

    eis::ExtractionOptions opts;
    opts.archivePath = m_archiveEdit->text().trimmed();
    opts.password = m_passwordEdit->text();
    opts.includeEntries = {e.path};
    opts.destination = QFileInfo(opts.archivePath).absolutePath() + QLatin1Char('/') + utils::stripArchiveExtensions(QFileInfo(opts.archivePath).fileName());
    opts.collision = eis::CollisionPolicy::Rename;
    opts.preservePermissions = AppSettings::instance().preservePermissions();
    opts.maxEntries = AppSettings::instance().maxEntries();
    opts.maxTotalBytes = AppSettings::instance().maxTotalBytes();

    const QString id = JobQueue::instance().enqueue(opts);
    Q_UNUSED(id)
    QMessageBox::information(this, QStringLiteral("Queued"), QStringLiteral("Extraction queued."));
}

void InspectorPage::setArchivePath(const QString &path)
{
    if (m_archiveEdit)
        m_archiveEdit->setText(path);
}

} // namespace eis::ui
