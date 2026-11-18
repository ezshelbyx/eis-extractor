#include "ui/ProgressWindow.h"

#include "core/Utils.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace eis::ui {

ProgressWindow::ProgressWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("E.I.S EXTRACTOR"));
    setModal(false);
    setMinimumWidth(420);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("EXTRACTION PROGRESS"), this);
    title->setObjectName("title");
    layout->addWidget(title);

    m_archiveLabel = new QLabel(QStringLiteral("Archive: --"), this);
    layout->addWidget(m_archiveLabel);

    m_fileLabel = new QLabel(QStringLiteral("Current: --"), this);
    layout->addWidget(m_fileLabel);

    m_percentLabel = new QLabel(QStringLiteral("0%"), this);
    m_percentLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_percentLabel);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    layout->addWidget(m_bar);

    m_filesLabel = new QLabel(QStringLiteral("Files: --"), this);
    layout->addWidget(m_filesLabel);

    m_speedLabel = new QLabel(QStringLiteral("Speed: --"), this);
    layout->addWidget(m_speedLabel);

    m_elapsedLabel = new QLabel(QStringLiteral("Elapsed: --"), this);
    layout->addWidget(m_elapsedLabel);

    m_etaLabel = new QLabel(QStringLiteral("Remaining: --"), this);
    layout->addWidget(m_etaLabel);

    m_destLabel = new QLabel(QStringLiteral("Destination: --"), this);
    m_destLabel->setWordWrap(true);
    layout->addWidget(m_destLabel);

    m_statusLabel = new QLabel(QStringLiteral("Status: Running"), this);
    layout->addWidget(m_statusLabel);

    auto *btnRow = new QHBoxLayout();
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        emit cancelRequested();
        m_cancelBtn->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("Status: Cancelling..."));
    });
    m_minBtn = new QPushButton(QStringLiteral("Minimize"), this);
    connect(m_minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    m_openBtn = new QPushButton(QStringLiteral("Open Destination"), this);
    m_openBtn->setEnabled(false);
    connect(m_openBtn, &QPushButton::clicked, this, &ProgressWindow::openDestination);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_minBtn);
    btnRow->addWidget(m_openBtn);
    layout->addLayout(btnRow);
}

void ProgressWindow::setArchiveCount(int count)
{
    Q_UNUSED(count);
}

void ProgressWindow::setArchiveInfo(int index, const QString &archiveName, const QString &destination)
{
    m_archiveLabel->setText(QStringLiteral("Archive %1: %2").arg(index + 1).arg(archiveName));
    m_destination = destination;
    m_destLabel->setText(QStringLiteral("Destination: %1").arg(destination));
}

void ProgressWindow::onProgress(const eis::ProgressInfo &info)
{
    if (info.indeterminate()) {
        m_bar->setRange(0, 0);
        m_percentLabel->setText(QStringLiteral("--%"));
    } else {
        m_bar->setRange(0, 100);
        const int pct = (info.bytesTotal > 0) ? (int)(100.0 * (double)info.bytesDone / (double)info.bytesTotal) : 0;
        m_bar->setValue(pct);
        m_percentLabel->setText(QStringLiteral("%1%").arg(pct));
    }
    if (!info.currentFile.isEmpty())
        m_fileLabel->setText(QStringLiteral("Current: %1").arg(info.currentFile));
    if (info.filesTotal >= 0)
        m_filesLabel->setText(QStringLiteral("Files: %1 / %2").arg(info.filesDone).arg(info.filesTotal));
    else
        m_filesLabel->setText(QStringLiteral("Files: %1").arg(info.filesDone));
    m_speedLabel->setText(QStringLiteral("Speed: %1").arg(eis::utils::humanSpeed(info.bytesPerSecond)));
    m_elapsedLabel->setText(QStringLiteral("Elapsed: %1").arg(eis::utils::humanDuration(info.elapsedMs)));
    if (!info.indeterminate() && info.bytesPerSecond > 0.0) {
        const qint64 remaining = info.bytesTotal - info.bytesDone;
        const qint64 etaMs = (qint64)((double)remaining / info.bytesPerSecond * 1000.0);
        m_etaLabel->setText(QStringLiteral("Remaining: %1").arg(eis::utils::humanDuration(etaMs)));
    } else {
        m_etaLabel->setText(QStringLiteral("Remaining: --"));
    }
}

void ProgressWindow::onArchiveFinished(bool ok, const QString &error)
{
    if (!ok) {
        m_statusLabel->setText(QStringLiteral("Status: Failed - %1").arg(error));
    }
}

void ProgressWindow::onAllFinished(bool ok)
{
    m_finished = true;
    m_success = ok;
    m_cancelBtn->setEnabled(false);
    if (ok) {
        m_statusLabel->setText(QStringLiteral("Status: Completed"));
        m_openBtn->setEnabled(true);
    } else {
        m_statusLabel->setText(QStringLiteral("Status: Failed"));
    }
}

void ProgressWindow::openDestination()
{
    if (!m_destination.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_destination));
}

void ProgressWindow::closeEvent(QCloseEvent *event)
{
    if (!m_finished) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

} // namespace eis::ui
