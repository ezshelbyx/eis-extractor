#include "ui/IntegrityPage.h"

#include "archive/ArchiveService.h"
#include "core/Utils.h"
#include "settings/AppSettings.h"
#include "integrity/IntegrityChecker.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace eis::ui {

IntegrityPage::IntegrityPage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *inputGroup = new QGroupBox(QStringLiteral("INTEGRITY LAB"), this);
    auto *form = new QFormLayout(inputGroup);
    m_archiveEdit = new QLineEdit(inputGroup);
    auto *archiveRow = new QHBoxLayout();
    archiveRow->addWidget(m_archiveEdit);
    auto *browseBtn = new QPushButton(QStringLiteral("Browse"), inputGroup);
    connect(browseBtn, &QPushButton::clicked, this, &IntegrityPage::browseArchive);
    archiveRow->addWidget(browseBtn);
    auto *aw = new QWidget(inputGroup);
    aw->setLayout(archiveRow);
    form->addRow(QStringLiteral("Archive"), aw);

    m_passwordEdit = new QLineEdit(inputGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password"), m_passwordEdit);

    m_expectedEdit = new QLineEdit(inputGroup);
    m_expectedEdit->setPlaceholderText(QStringLiteral("optional expected SHA-256"));
    form->addRow(QStringLiteral("Expected SHA-256"), m_expectedEdit);

    auto *btnRow = new QHBoxLayout();
    auto *testBtn = new QPushButton(QStringLiteral("Run Integrity Test"), inputGroup);
    testBtn->setObjectName("primary");
    connect(testBtn, &QPushButton::clicked, this, &IntegrityPage::runTest);
    auto *shaBtn = new QPushButton(QStringLiteral("Compute SHA-256"), inputGroup);
    connect(shaBtn, &QPushButton::clicked, this, &IntegrityPage::computeSha);
    btnRow->addWidget(testBtn);
    btnRow->addWidget(shaBtn);
    form->addRow(btnRow);

    mainLayout->addWidget(inputGroup);

    m_resultEdit = new QTextEdit(this);
    m_resultEdit->setReadOnly(true);
    mainLayout->addWidget(m_resultEdit, 1);
}

void IntegrityPage::browseArchive()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select archive"), AppSettings::instance().lastDirectory(), utils::archiveNameFilter());
    if (!file.isEmpty()) {
        m_archiveEdit->setText(file);
        AppSettings::instance().setLastDirectory(QFileInfo(file).absolutePath());
    }
}

void IntegrityPage::runTest()
{
    const QString path = m_archiveEdit->text().trimmed();
    if (path.isEmpty())
        return;
    IntegrityChecker checker;
    const IntegrityResult res = checker.check(path, m_passwordEdit->text(), m_expectedEdit->text().trimmed());
    QString text;
    text += QStringLiteral("Archive: %1\n").arg(path);
    text += QStringLiteral("Backend: %1\n").arg(res.backendName);
    text += QStringLiteral("Format: %1\n").arg(res.formatName);
    text += QStringLiteral("Completed: %1\n").arg(res.completed ? QStringLiteral("yes") : QStringLiteral("no"));
    text += QStringLiteral("Result: %1\n").arg(res.ok ? QStringLiteral("OK") : QStringLiteral("FAILED"));
    if (!res.error.isEmpty())
        text += QStringLiteral("Error: %1\n").arg(res.error);
    text += QStringLiteral("SHA-256: %1\n").arg(res.sha256);
    if (res.expectedMatch >= 0)
        text += QStringLiteral("Expected match: %1\n").arg(res.expectedMatch == 1 ? QStringLiteral("yes") : QStringLiteral("no"));
    text += QStringLiteral("Bytes read: %1\n").arg(utils::humanBytes(res.bytesRead));
    text += QStringLiteral("Duration: %1\n").arg(utils::humanDuration(res.durationMs));
    m_resultEdit->setPlainText(text);
}

void IntegrityPage::computeSha()
{
    const QString path = m_archiveEdit->text().trimmed();
    if (path.isEmpty())
        return;
    ArchiveService service;
    const QString sha = service.sha256File(path);
    m_resultEdit->setPlainText(QStringLiteral("SHA-256: %1").arg(sha));
}

} // namespace eis::ui

