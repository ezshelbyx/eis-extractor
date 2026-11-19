#include "ui/MainWindow.h"

#include "core/Types.h"
#include "core/Utils.h"
#include "queue/JobQueue.h"
#include "settings/AppSettings.h"
#include "ui/CompressionPage.h"
#include "ui/DiscImagePage.h"
#include "ui/InspectorPage.h"
#include "ui/IntegrityPage.h"
#include "ui/LogPage.h"
#include "ui/OperationsPage.h"
#include "ui/OverviewPage.h"
#include "ui/SettingsPage.h"
#include "ui/Theme.h"
#include "ui/VaultPage.h"
#include "ui/VerificationPage.h"
#include "widgets/HudWidgets.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

namespace eis::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("E.I.S ARCHIVE"));
    resize(1280, 820);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *sidebar = new QWidget(central);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(220);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(10, 14, 10, 14);
    sideLayout->setSpacing(6);

    auto *brand = new QLabel(QStringLiteral("E.I.S CYBERCORE"), sidebar);
    brand->setObjectName("brand");
    sideLayout->addWidget(brand);
    auto *subtitle = new QLabel(QStringLiteral("EXTRACTOR"), sidebar);
    subtitle->setObjectName("subtitle");
    sideLayout->addWidget(subtitle);
    sideLayout->addSpacing(10);

    const QStringList labels = {
        QStringLiteral("OVERVIEW"),
        QStringLiteral("EXTRACTION OPERATIONS"),
        QStringLiteral("COMPRESSION ENGINE"),
        QStringLiteral("ARCHIVE INSPECTOR"),
        QStringLiteral("DISC IMAGES"),
        QStringLiteral("PASSWORD VAULT"),
        QStringLiteral("FILE VERIFICATION"),
        QStringLiteral("INTEGRITY LAB"),
        QStringLiteral("ACTIVITY LOG"),
        QStringLiteral("SETTINGS")
    };

    m_navButtons = new QVector<QPushButton*>();
    for (int i = 0; i < labels.size(); ++i) {
        auto *btn = new eis::widgets::NavButton(labels[i], sidebar);
        connect(btn, &QPushButton::clicked, this, [this, i]() { switchPage(i); });
        sideLayout->addWidget(btn);
        m_navButtons->append(btn);
    }
    sideLayout->addStretch();

    m_stack = new QStackedWidget(central);
    m_overview = new OverviewPage(this);
    m_operations = new OperationsPage(this);
    m_compression = new CompressionPage(this);
    m_inspector = new InspectorPage(this);
    m_discImages = new DiscImagePage(this);
    m_vault = new VaultPage(this);
    m_verification = new VerificationPage(this);
    m_integrity = new IntegrityPage(this);
    m_log = new LogPage(this);
    m_settings = new SettingsPage(this);

    m_stack->addWidget(m_overview);
    m_stack->addWidget(m_operations);
    m_stack->addWidget(m_compression);
    m_stack->addWidget(m_inspector);
    m_stack->addWidget(m_discImages);
    m_stack->addWidget(m_vault);
    m_stack->addWidget(m_verification);
    m_stack->addWidget(m_integrity);
    m_stack->addWidget(m_log);
    m_stack->addWidget(m_settings);

    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(m_stack, 1);
    setCentralWidget(central);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    auto *openAct = fileMenu->addAction(QStringLiteral("Open archive..."));
    connect(openAct, &QAction::triggered, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open archive"), AppSettings::instance().lastDirectory(),
            utils::archiveNameFilter());
        if (!file.isEmpty())
            openArchive(file);
    });
    fileMenu->addSeparator();
    auto *quitAct = fileMenu->addAction(QStringLiteral("Quit"));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    auto *aboutAct = helpMenu->addAction(QStringLiteral("About"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("About"),
                                 QStringLiteral("E.I.S ARCHIVE\nE.I.S CYBERCORE\n"
                                                "Native Linux archive, disc image and password manager"));
    });

    connect(&JobQueue::instance(), &JobQueue::jobAdded, this, &MainWindow::onJobChanged);
    connect(&JobQueue::instance(), &JobQueue::jobUpdated, this, &MainWindow::onJobChanged);
    connect(&JobQueue::instance(), &JobQueue::jobRemoved, this, &MainWindow::onJobChanged);
    connect(&JobQueue::instance(), &JobQueue::progressUpdated, this, &MainWindow::onJobChanged);

    connect(m_operations, &OperationsPage::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 4000);
    });
    connect(m_compression, &CompressionPage::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 4000);
    });
    connect(m_vault, &VaultPage::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 4000);
    });
    connect(m_verification, &VerificationPage::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 4000);
    });
    connect(m_discImages, &DiscImagePage::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 4000);
    });

    auto *toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    auto *compressAct = toolsMenu->addAction(QStringLiteral("Compress files..."));
    connect(compressAct, &QAction::triggered, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Select files to compress"),
            AppSettings::instance().lastDirectory(), QStringLiteral("All files (*)"));
        if (!files.isEmpty()) {
            m_compression->setSources(files);
            switchPage(2);
        }
    });
    auto *vaultAct = toolsMenu->addAction(QStringLiteral("Password Vault"));
    connect(vaultAct, &QAction::triggered, this, [this]() { switchPage(5); });
    auto *imagesAct = toolsMenu->addAction(QStringLiteral("Disc Images"));
    connect(imagesAct, &QAction::triggered, this, [this]() { switchPage(4); });
    auto *verifyAct = toolsMenu->addAction(QStringLiteral("Verify files"));
    connect(verifyAct, &QAction::triggered, this, [this]() { switchPage(6); });

    applyTheme();
    switchPage(0);
}

void MainWindow::switchPage(int index)
{
    m_stack->setCurrentIndex(index);
    for (int i = 0; i < m_navButtons->size(); ++i) {
        m_navButtons->at(i)->setChecked(i == index);
    }
}

void MainWindow::onJobChanged()
{
    m_overview->refresh(JobQueue::instance().jobs());
}

void MainWindow::applyTheme()
{
    const QString theme = AppSettings::instance().theme();
    const bool compact = AppSettings::instance().compactMode();
    const bool reduced = AppSettings::instance().reducedMotion();
    qApp->setStyleSheet(eis::ui::styleSheet(theme, compact, reduced));
}

void MainWindow::openArchive(const QString &path)
{
    if (m_operations)
        m_operations->setArchivePath(path);
    if (m_inspector)
        m_inspector->setArchivePath(path);
    switchPage(3); // inspector
}

} // namespace eis::ui
