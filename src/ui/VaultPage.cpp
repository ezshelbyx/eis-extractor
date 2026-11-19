#include "ui/VaultPage.h"

#include "core/Utils.h"
#include "vault/PasswordGenerator.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>

namespace eis::ui {

VaultPage::VaultPage(QWidget *parent)
    : QWidget(parent)
    , m_vault(PasswordVault::instance())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    buildLockedPage();
    buildUnlockedPage();
    m_stack->addWidget(m_lockedPage);
    m_stack->addWidget(m_unlockedPage);
    layout->addWidget(m_stack);

    m_autoLockTimer = new QTimer(this);
    m_autoLockTimer->setSingleShot(true);
    connect(m_autoLockTimer, &QTimer::timeout, this, &VaultPage::autoLockTimeout);

    for (QWidget *widget : findChildren<QWidget *>())
        widget->installEventFilter(this);

    setUnlockedUi(m_vault.isUnlocked());
}

void VaultPage::buildLockedPage()
{
    m_lockedPage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_lockedPage);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("PASSWORD VAULT"), m_lockedPage);
    title->setObjectName("title");
    layout->addWidget(title);

    auto *scan = new QLabel(
        QStringLiteral("Argon2id key derivation · AES-256-GCM authenticated encryption\n"
                       "The master password is never stored. Encrypted backups stay encrypted."),
        m_lockedPage);
    scan->setObjectName("subtitle");
    layout->addWidget(scan);

    auto *group = new QGroupBox(QStringLiteral("VAULT ACCESS"), m_lockedPage);
    auto *form = new QFormLayout(group);
    auto *pathRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(group);
    m_pathEdit->setText(m_vault.path().isEmpty() ? PasswordVault::defaultVaultPath()
                                                 : m_vault.path());
    auto *browse = new QPushButton(QStringLiteral("Browse"), group);
    connect(browse, &QPushButton::clicked, this, &VaultPage::browseVaultPath);
    pathRow->addWidget(m_pathEdit);
    pathRow->addWidget(browse);
    auto *pathWidget = new QWidget(group);
    pathWidget->setLayout(pathRow);
    form->addRow(QStringLiteral("Vault file"), pathWidget);

    m_masterEdit = new QLineEdit(group);
    m_masterEdit->setEchoMode(QLineEdit::Password);
    m_masterEdit->setPlaceholderText(QStringLiteral("master password"));
    connect(m_masterEdit, &QLineEdit::returnPressed, this, &VaultPage::unlockVault);
    form->addRow(QStringLiteral("Master password"), m_masterEdit);

    auto *buttons = new QHBoxLayout();
    auto *unlockButton = new QPushButton(QStringLiteral("Unlock Vault"), group);
    unlockButton->setObjectName("primary");
    connect(unlockButton, &QPushButton::clicked, this, &VaultPage::unlockVault);
    auto *createButton = new QPushButton(QStringLiteral("Create New Vault"), group);
    connect(createButton, &QPushButton::clicked, this, &VaultPage::createVault);
    buttons->addWidget(unlockButton);
    buttons->addWidget(createButton);
    buttons->addStretch();
    form->addRow(buttons);

    m_lockedInfo = new QLabel(
        QStringLiteral("Losing the master password makes the vault unrecoverable unless you "
                       "have an encrypted backup and remember its password."),
        group);
    m_lockedInfo->setWordWrap(true);
    form->addRow(m_lockedInfo);

    layout->addWidget(group);
    layout->addStretch();
}

void VaultPage::buildUnlockedPage()
{
    m_unlockedPage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_unlockedPage);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout();
    m_vaultPathLabel = new QLabel(m_unlockedPage);
    m_vaultPathLabel->setObjectName("section");
    auto *lockButton = new QPushButton(QStringLiteral("Lock Vault"), m_unlockedPage);
    lockButton->setObjectName("danger");
    connect(lockButton, &QPushButton::clicked, this, &VaultPage::lockVault);
    auto *saveButton = new QPushButton(QStringLiteral("Save"), m_unlockedPage);
    connect(saveButton, &QPushButton::clicked, this, &VaultPage::saveVault);
    header->addWidget(m_vaultPathLabel, 1);
    header->addWidget(saveButton);
    header->addWidget(lockButton);
    layout->addLayout(header);

    m_tabs = new QTabWidget(m_unlockedPage);
    layout->addWidget(m_tabs, 1);

    // ------------------------------------------------------------- entries
    auto *entriesTab = new QWidget(m_tabs);
    auto *entriesLayout = new QVBoxLayout(entriesTab);
    entriesLayout->setContentsMargins(6, 6, 6, 6);

    auto *filterRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(entriesTab);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search title, username, URL, tags, notes..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &VaultPage::refreshEntryTable);
    m_categoryFilter = new QComboBox(entriesTab);
    connect(m_categoryFilter, &QComboBox::currentIndexChanged, this, &VaultPage::refreshEntryTable);
    m_favoritesOnly = new QCheckBox(QStringLiteral("Favorites"), entriesTab);
    connect(m_favoritesOnly, &QCheckBox::toggled, this, &VaultPage::refreshEntryTable);
    filterRow->addWidget(m_searchEdit, 1);
    filterRow->addWidget(m_categoryFilter);
    filterRow->addWidget(m_favoritesOnly);
    entriesLayout->addLayout(filterRow);

    auto *entrySplitter = new QSplitter(Qt::Horizontal, entriesTab);
    m_entryTable = new QTableWidget(entrySplitter);
    m_entryTable->setColumnCount(4);
    m_entryTable->setHorizontalHeaderLabels(
        {QStringLiteral("Title"), QStringLiteral("Username"), QStringLiteral("Category"),
         QStringLiteral("Favorite")});
    m_entryTable->horizontalHeader()->setStretchLastSection(true);
    m_entryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_entryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_entryTable->setAlternatingRowColors(true);
    connect(m_entryTable, &QTableWidget::itemSelectionChanged,
            this, &VaultPage::entrySelectionChanged);
    entrySplitter->addWidget(m_entryTable);

    auto *editorScroll = new QScrollArea(entrySplitter);
    editorScroll->setWidgetResizable(true);
    auto *editor = new QWidget(editorScroll);
    auto *editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(8, 8, 8, 8);

    auto *editorForm = new QFormLayout();
    m_typeBox = new QComboBox(editor);
    for (const QString &type : PasswordVault::entryTypes())
        m_typeBox->addItem(PasswordVault::entryTypeLabel(type), type);
    editorForm->addRow(QStringLiteral("Type"), m_typeBox);

    m_titleEdit = new QLineEdit(editor);
    editorForm->addRow(QStringLiteral("Title"), m_titleEdit);
    m_usernameEdit = new QLineEdit(editor);
    editorForm->addRow(QStringLiteral("Username"), m_usernameEdit);

    auto *passwordRow = new QHBoxLayout();
    m_entryPasswordEdit = new QLineEdit(editor);
    m_entryPasswordEdit->setEchoMode(QLineEdit::Password);
    connect(m_entryPasswordEdit, &QLineEdit::textChanged, this, &VaultPage::updateStrength);
    auto *copyPasswordButton = new QPushButton(QStringLiteral("Copy"), editor);
    connect(copyPasswordButton, &QPushButton::clicked, this, &VaultPage::copyEntryPassword);
    m_showPasswordCheck = new QCheckBox(QStringLiteral("Show"), editor);
    connect(m_showPasswordCheck, &QCheckBox::toggled, this, [this](bool show) {
        m_entryPasswordEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
    passwordRow->addWidget(m_entryPasswordEdit, 1);
    passwordRow->addWidget(copyPasswordButton);
    passwordRow->addWidget(m_showPasswordCheck);
    auto *passwordWidget = new QWidget(editor);
    passwordWidget->setLayout(passwordRow);
    editorForm->addRow(QStringLiteral("Password"), passwordWidget);

    m_strengthLabel = new QLabel(QStringLiteral("Strength: --"), editor);
    editorForm->addRow(m_strengthLabel);

    m_urlEdit = new QLineEdit(editor);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://..."));
    editorForm->addRow(QStringLiteral("Website / address"), m_urlEdit);
    m_emailEdit = new QLineEdit(editor);
    editorForm->addRow(QStringLiteral("Email"), m_emailEdit);
    m_categoryEdit = new QLineEdit(editor);
    m_categoryEdit->setPlaceholderText(QStringLiteral("e.g. Work, Personal"));
    editorForm->addRow(QStringLiteral("Category"), m_categoryEdit);
    m_tagsEdit = new QLineEdit(editor);
    m_tagsEdit->setPlaceholderText(QStringLiteral("comma, separated, tags"));
    editorForm->addRow(QStringLiteral("Tags"), m_tagsEdit);
    m_favoriteCheck = new QCheckBox(QStringLiteral("Favorite"), editor);
    editorForm->addRow(m_favoriteCheck);
    m_historyLabel = new QLabel(QStringLiteral("History: 0 previous passwords"), editor);
    editorForm->addRow(m_historyLabel);

    m_notesEdit = new QPlainTextEdit(editor);
    m_notesEdit->setPlaceholderText(QStringLiteral("Secure notes"));
    m_notesEdit->setMaximumHeight(80);
    editorForm->addRow(QStringLiteral("Notes"), m_notesEdit);

    m_customFieldsEdit = new QPlainTextEdit(editor);
    m_customFieldsEdit->setPlaceholderText(QStringLiteral("Custom fields, one per line: key=value"));
    m_customFieldsEdit->setMaximumHeight(60);
    editorForm->addRow(QStringLiteral("Custom fields"), m_customFieldsEdit);
    editorLayout->addLayout(editorForm);

    auto *generatorGroup = new QGroupBox(QStringLiteral("GENERATOR"), editor);
    auto *generatorLayout = new QFormLayout(generatorGroup);
    m_generatorLength = new QSpinBox(generatorGroup);
    m_generatorLength->setRange(8, 128);
    m_generatorLength->setValue(20);
    generatorLayout->addRow(QStringLiteral("Length"), m_generatorLength);
    auto *classesRow = new QHBoxLayout();
    m_genUpper = new QCheckBox(QStringLiteral("A-Z"), generatorGroup);
    m_genUpper->setChecked(true);
    m_genLower = new QCheckBox(QStringLiteral("a-z"), generatorGroup);
    m_genLower->setChecked(true);
    m_genDigits = new QCheckBox(QStringLiteral("0-9"), generatorGroup);
    m_genDigits->setChecked(true);
    m_genSymbols = new QCheckBox(QStringLiteral("Symbols"), generatorGroup);
    m_genSymbols->setChecked(true);
    classesRow->addWidget(m_genUpper);
    classesRow->addWidget(m_genLower);
    classesRow->addWidget(m_genDigits);
    classesRow->addWidget(m_genSymbols);
    classesRow->addStretch();
    generatorLayout->addRow(classesRow);
    m_genAmbiguous = new QCheckBox(QStringLiteral("Exclude ambiguous characters (O0Il1...)"),
                                   generatorGroup);
    m_genAmbiguous->setChecked(true);
    generatorLayout->addRow(m_genAmbiguous);
    auto *generateButton = new QPushButton(QStringLiteral("Generate Password"), generatorGroup);
    connect(generateButton, &QPushButton::clicked, this, &VaultPage::generateEntryPassword);
    generatorLayout->addRow(generateButton);
    editorLayout->addWidget(generatorGroup);

    auto *entryButtons = new QHBoxLayout();
    auto *newButton = new QPushButton(QStringLiteral("New"), editor);
    connect(newButton, &QPushButton::clicked, this, &VaultPage::newEntry);
    auto *saveEntryButton = new QPushButton(QStringLiteral("Save Entry"), editor);
    saveEntryButton->setObjectName("primary");
    connect(saveEntryButton, &QPushButton::clicked, this, &VaultPage::saveEntry);
    auto *deleteButton = new QPushButton(QStringLiteral("Delete"), editor);
    deleteButton->setObjectName("danger");
    connect(deleteButton, &QPushButton::clicked, this, &VaultPage::deleteEntry);
    auto *copyUserButton = new QPushButton(QStringLiteral("Copy Username"), editor);
    connect(copyUserButton, &QPushButton::clicked, this, &VaultPage::copyEntryUsername);
    entryButtons->addWidget(newButton);
    entryButtons->addWidget(saveEntryButton);
    entryButtons->addWidget(deleteButton);
    entryButtons->addWidget(copyUserButton);
    entryButtons->addStretch();
    editorLayout->addLayout(entryButtons);
    editorLayout->addStretch();

    editorScroll->setWidget(editor);
    entrySplitter->addWidget(editorScroll);
    entrySplitter->setStretchFactor(0, 2);
    entrySplitter->setStretchFactor(1, 3);
    entriesLayout->addWidget(entrySplitter, 1);
    m_tabs->addTab(entriesTab, QStringLiteral("ENTRIES"));

    // ---------------------------------------------------- archive passwords
    auto *archiveTab = new QWidget(m_tabs);
    auto *archiveLayout = new QVBoxLayout(archiveTab);
    archiveLayout->setContentsMargins(6, 6, 6, 6);

    m_archiveTable = new QTableWidget(archiveTab);
    m_archiveTable->setColumnCount(4);
    m_archiveTable->setHorizontalHeaderLabels(
        {QStringLiteral("Archive"), QStringLiteral("Path"), QStringLiteral("Group"),
         QStringLiteral("Modified")});
    m_archiveTable->horizontalHeader()->setStretchLastSection(true);
    m_archiveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_archiveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_archiveTable->setAlternatingRowColors(true);
    connect(m_archiveTable, &QTableWidget::itemSelectionChanged,
            this, &VaultPage::archiveSelectionChanged);
    archiveLayout->addWidget(m_archiveTable, 1);

    auto *archiveGroup = new QGroupBox(QStringLiteral("ARCHIVE PASSWORD RECORD"), archiveTab);
    auto *archiveForm = new QFormLayout(archiveGroup);
    m_archiveNameEdit = new QLineEdit(archiveGroup);
    m_archiveNameEdit->setPlaceholderText(QStringLiteral("archive file name"));
    archiveForm->addRow(QStringLiteral("Archive name"), m_archiveNameEdit);
    auto *archivePathRow = new QHBoxLayout();
    m_archivePathEdit = new QLineEdit(archiveGroup);
    m_archivePathEdit->setPlaceholderText(QStringLiteral("optional full path for exact matching"));
    auto *browseArchiveButton = new QPushButton(QStringLiteral("Browse"), archiveGroup);
    connect(browseArchiveButton, &QPushButton::clicked, this, &VaultPage::browseArchivePath);
    archivePathRow->addWidget(m_archivePathEdit);
    archivePathRow->addWidget(browseArchiveButton);
    auto *archivePathWidget = new QWidget(archiveGroup);
    archivePathWidget->setLayout(archivePathRow);
    archiveForm->addRow(QStringLiteral("Archive path"), archivePathWidget);

    m_archivePasswordEdit = new QLineEdit(archiveGroup);
    m_archivePasswordEdit->setEchoMode(QLineEdit::Password);
    auto *archivePasswordRow = new QHBoxLayout();
    auto *copyArchiveButton = new QPushButton(QStringLiteral("Copy"), archiveGroup);
    connect(copyArchiveButton, &QPushButton::clicked, this, &VaultPage::copyArchivePassword);
    auto *showArchiveCheck = new QCheckBox(QStringLiteral("Show"), archiveGroup);
    connect(showArchiveCheck, &QCheckBox::toggled, this, [this](bool show) {
        m_archivePasswordEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
    archivePasswordRow->addWidget(m_archivePasswordEdit, 1);
    archivePasswordRow->addWidget(copyArchiveButton);
    archivePasswordRow->addWidget(showArchiveCheck);
    auto *archivePasswordWidget = new QWidget(archiveGroup);
    archivePasswordWidget->setLayout(archivePasswordRow);
    archiveForm->addRow(QStringLiteral("Password"), archivePasswordWidget);

    m_archiveGroupEdit = new QLineEdit(archiveGroup);
    archiveForm->addRow(QStringLiteral("Group"), m_archiveGroupEdit);
    m_archiveNoteEdit = new QLineEdit(archiveGroup);
    archiveForm->addRow(QStringLiteral("Note"), m_archiveNoteEdit);

    auto *archiveButtons = new QHBoxLayout();
    auto *newArchiveButton = new QPushButton(QStringLiteral("New Record"), archiveGroup);
    connect(newArchiveButton, &QPushButton::clicked, this, &VaultPage::newArchivePassword);
    auto *saveArchiveButton = new QPushButton(QStringLiteral("Save Record"), archiveGroup);
    saveArchiveButton->setObjectName("primary");
    connect(saveArchiveButton, &QPushButton::clicked, this, &VaultPage::saveArchivePassword);
    auto *deleteArchiveButton = new QPushButton(QStringLiteral("Delete"), archiveGroup);
    deleteArchiveButton->setObjectName("danger");
    connect(deleteArchiveButton, &QPushButton::clicked, this, &VaultPage::deleteArchivePassword);
    archiveButtons->addWidget(newArchiveButton);
    archiveButtons->addWidget(saveArchiveButton);
    archiveButtons->addWidget(deleteArchiveButton);
    archiveButtons->addStretch();
    archiveForm->addRow(archiveButtons);
    archiveLayout->addWidget(archiveGroup);
    m_tabs->addTab(archiveTab, QStringLiteral("ARCHIVE PASSWORDS"));

    // -------------------------------------------------------------- backup
    auto *backupTab = new QWidget(m_tabs);
    auto *backupLayout = new QVBoxLayout(backupTab);
    backupLayout->setContentsMargins(12, 12, 12, 12);
    auto *backupInfo = new QLabel(
        QStringLiteral("Backups are always encrypted. Export writes a complete encrypted copy; "
                       "import merges another encrypted vault into this one; restore replaces "
                       "this vault's contents with the backup. The backup password is required "
                       "to import or restore."),
        backupTab);
    backupInfo->setWordWrap(true);
    backupLayout->addWidget(backupInfo);

    auto *backupButtons = new QHBoxLayout();
    auto *exportButton = new QPushButton(QStringLiteral("Export Encrypted Backup"), backupTab);
    exportButton->setObjectName("primary");
    connect(exportButton, &QPushButton::clicked, this, &VaultPage::exportBackup);
    auto *importButton = new QPushButton(QStringLiteral("Import Encrypted Vault"), backupTab);
    connect(importButton, &QPushButton::clicked, this, &VaultPage::importBackup);
    auto *restoreButton = new QPushButton(QStringLiteral("Restore From Backup"), backupTab);
    restoreButton->setObjectName("danger");
    connect(restoreButton, &QPushButton::clicked, this, &VaultPage::restoreBackup);
    backupButtons->addWidget(exportButton);
    backupButtons->addWidget(importButton);
    backupButtons->addWidget(restoreButton);
    backupButtons->addStretch();
    backupLayout->addLayout(backupButtons);

    auto *autoLockGroup = new QGroupBox(QStringLiteral("LOCKING"), backupTab);
    auto *autoLockForm = new QFormLayout(autoLockGroup);
    m_autoLockSpin = new QSpinBox(autoLockGroup);
    m_autoLockSpin->setRange(1, 60);
    m_autoLockSpin->setValue(5);
    m_autoLockSpin->setSuffix(QStringLiteral(" minutes"));
    connect(m_autoLockSpin, &QSpinBox::valueChanged, this, &VaultPage::updateAutoLock);
    autoLockForm->addRow(QStringLiteral("Auto-lock after inactivity"), m_autoLockSpin);
    auto *clipboardInfo = new QLabel(
        QStringLiteral("Copied passwords are cleared from the clipboard after 30 seconds. "
                       "Clipboard clearing is best effort; other applications may still have "
                       "read the value before it is cleared."),
        autoLockGroup);
    clipboardInfo->setWordWrap(true);
    autoLockForm->addRow(clipboardInfo);
    backupLayout->addWidget(autoLockGroup);
    backupLayout->addStretch();
    m_tabs->addTab(backupTab, QStringLiteral("BACKUP & SECURITY"));

    m_statusLabel = new QLabel(m_unlockedPage);
    m_statusLabel->setObjectName("subtitle");
    layout->addWidget(m_statusLabel);
}

void VaultPage::setUnlockedUi(bool unlocked)
{
    m_stack->setCurrentWidget(unlocked ? m_unlockedPage : m_lockedPage);
    if (unlocked) {
        m_vaultPathLabel->setText(QStringLiteral("UNLOCKED  ·  %1").arg(m_vault.path()));
        m_masterEdit->clear();
        refreshEntryTable();
        refreshArchiveTable();
        resetActivity();
    } else {
        m_autoLockTimer->stop();
        m_entryTable->setRowCount(0);
        m_archiveTable->setRowCount(0);
        clearEntryEditor();
        clearArchiveEditor();
        m_pathEdit->setText(m_vault.path().isEmpty() ? PasswordVault::defaultVaultPath()
                                                     : m_vault.path());
        m_lockedInfo->setText(
            m_vault.vaultExists(m_pathEdit->text())
                ? QStringLiteral("Vault found. Enter the master password to unlock.")
                : QStringLiteral("No vault at this path yet. Create one with a master password "
                                 "of at least 8 characters."));
    }
}

void VaultPage::browseVaultPath()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Vault file"), m_pathEdit->text(),
        QStringLiteral("E.I.S vault (*.eisvault);;All files (*)"));
    if (!file.isEmpty())
        m_pathEdit->setText(file);
}

void VaultPage::createVault()
{
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty())
        return;
    bool ok = false;
    const QString master = QInputDialog::getText(
        this, QStringLiteral("Create Vault"), QStringLiteral("Master password:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;
    const QString confirm = QInputDialog::getText(
        this, QStringLiteral("Create Vault"), QStringLiteral("Confirm master password:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;
    if (master != confirm) {
        QMessageBox::warning(this, QStringLiteral("Vault"), QStringLiteral("Passwords do not match."));
        return;
    }
    QString error;
    if (!m_vault.create(path, master, &error)) {
        QMessageBox::warning(this, QStringLiteral("Vault"), error);
        return;
    }
    setUnlockedUi(true);
    appendStatus(QStringLiteral("Vault created: %1").arg(path));
    emit statusMessage(QStringLiteral("Vault created"));
}

void VaultPage::unlockVault()
{
    const QString path = m_pathEdit->text().trimmed();
    const QString master = m_masterEdit->text();
    if (path.isEmpty() || master.isEmpty())
        return;
    QString error;
    if (!m_vault.unlock(path, master, &error)) {
        QMessageBox::warning(this, QStringLiteral("Unlock failed"), error);
        m_masterEdit->selectAll();
        m_masterEdit->setFocus();
        return;
    }
    setUnlockedUi(true);
    appendStatus(QStringLiteral("Vault unlocked."));
}

void VaultPage::lockVault()
{
    if (!m_vault.isUnlocked())
        return;
    QString error;
    if (!m_vault.save(&error))
        QMessageBox::warning(this, QStringLiteral("Vault"), error);
    m_vault.lock();
    setUnlockedUi(false);
    appendStatus(QStringLiteral("Vault locked."));
}

void VaultPage::saveVault()
{
    if (persist())
        appendStatus(QStringLiteral("Vault saved."));
}

bool VaultPage::persist()
{
    if (!m_vault.isUnlocked())
        return false;
    QString error;
    if (!m_vault.save(&error)) {
        QMessageBox::warning(this, QStringLiteral("Vault save failed"), error);
        return false;
    }
    return true;
}

void VaultPage::refreshEntryTable()
{
    if (!m_vault.isUnlocked())
        return;

    const QString keepId = m_currentEntryId;
    const QString currentCategory = m_categoryFilter->currentText();
    m_categoryFilter->blockSignals(true);
    m_categoryFilter->clear();
    m_categoryFilter->addItem(QStringLiteral("All categories"));
    for (const QString &category : m_vault.categories())
        m_categoryFilter->addItem(category);
    const int categoryIndex = m_categoryFilter->findText(currentCategory);
    m_categoryFilter->setCurrentIndex(categoryIndex >= 0 ? categoryIndex : 0);
    m_categoryFilter->blockSignals(false);

    const QString category = m_categoryFilter->currentIndex() <= 0
                                 ? QString()
                                 : m_categoryFilter->currentText();
    const QVector<VaultEntry> entries = m_vault.searchEntries(
        m_searchEdit->text(), category, m_favoritesOnly->isChecked());

    m_entryTable->blockSignals(true);
    m_entryTable->setRowCount(0);
    int selectRow = -1;
    for (const VaultEntry &entry : entries) {
        const int row = m_entryTable->rowCount();
        m_entryTable->insertRow(row);
        auto *title = new QTableWidgetItem(entry.title);
        title->setData(Qt::UserRole, entry.id);
        m_entryTable->setItem(row, 0, title);
        m_entryTable->setItem(row, 1, new QTableWidgetItem(entry.username));
        m_entryTable->setItem(row, 2, new QTableWidgetItem(entry.category));
        m_entryTable->setItem(row, 3, new QTableWidgetItem(entry.favorite
                                                               ? QStringLiteral("yes")
                                                               : QString()));
        if (entry.id == keepId)
            selectRow = row;
    }
    m_entryTable->blockSignals(false);
    if (selectRow >= 0)
        m_entryTable->selectRow(selectRow);
    else if (m_entryTable->rowCount() > 0 && keepId.isEmpty())
        m_entryTable->selectRow(0);
}

QString VaultPage::selectedEntryId() const
{
    const int row = m_entryTable->currentRow();
    if (row < 0)
        return QString();
    auto *item = m_entryTable->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void VaultPage::entrySelectionChanged()
{
    resetActivity();
    const QString id = selectedEntryId();
    if (id.isEmpty()) {
        clearEntryEditor();
        return;
    }
    for (const VaultEntry &entry : m_vault.entries()) {
        if (entry.id == id) {
            fillEntryEditor(entry);
            return;
        }
    }
}

void VaultPage::clearEntryEditor()
{
    m_currentEntryId.clear();
    m_typeBox->setCurrentIndex(0);
    m_titleEdit->clear();
    m_usernameEdit->clear();
    m_entryPasswordEdit->clear();
    m_urlEdit->clear();
    m_emailEdit->clear();
    m_categoryEdit->clear();
    m_tagsEdit->clear();
    m_notesEdit->clear();
    m_customFieldsEdit->clear();
    m_favoriteCheck->setChecked(false);
    m_showPasswordCheck->setChecked(false);
    m_strengthLabel->setText(QStringLiteral("Strength: --"));
    m_historyLabel->setText(QStringLiteral("History: 0 previous passwords"));
}

void VaultPage::fillEntryEditor(const VaultEntry &entry)
{
    m_currentEntryId = entry.id;
    const int typeIndex = m_typeBox->findData(entry.type);
    m_typeBox->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    m_titleEdit->setText(entry.title);
    m_usernameEdit->setText(entry.username);
    m_entryPasswordEdit->setText(entry.password);
    m_urlEdit->setText(entry.url);
    m_emailEdit->setText(entry.email);
    m_categoryEdit->setText(entry.category);
    m_tagsEdit->setText(entry.tags.join(QStringLiteral(", ")));
    m_notesEdit->setPlainText(entry.notes);
    QStringList custom;
    for (auto it = entry.customFields.constBegin(); it != entry.customFields.constEnd(); ++it)
        custom << QStringLiteral("%1=%2").arg(it.key(), it.value());
    m_customFieldsEdit->setPlainText(custom.join(QLatin1Char('\n')));
    m_favoriteCheck->setChecked(entry.favorite);
    m_showPasswordCheck->setChecked(false);
    m_historyLabel->setText(QStringLiteral("History: %1 previous password(s)")
                                .arg(entry.history.size()));
    updateStrength();
}

void VaultPage::newEntry()
{
    m_entryTable->clearSelection();
    clearEntryEditor();
    m_titleEdit->setFocus();
    resetActivity();
}

void VaultPage::saveEntry()
{
    if (!m_vault.isUnlocked())
        return;
    if (m_titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Entry"), QStringLiteral("A title is required."));
        return;
    }

    VaultEntry entry;
    entry.id = m_currentEntryId;
    entry.type = m_typeBox->currentData().toString();
    entry.title = m_titleEdit->text().trimmed();
    entry.username = m_usernameEdit->text();
    entry.password = m_entryPasswordEdit->text();
    entry.url = m_urlEdit->text();
    entry.email = m_emailEdit->text();
    entry.category = m_categoryEdit->text().trimmed();
    for (const QString &tag : m_tagsEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty())
            entry.tags << trimmed;
    }
    entry.notes = m_notesEdit->toPlainText();
    entry.favorite = m_favoriteCheck->isChecked();

    for (const QString &line : m_customFieldsEdit->toPlainText().split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty())
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            QMessageBox::warning(this, QStringLiteral("Custom fields"),
                                 QStringLiteral("Use key=value per line: %1").arg(line));
            return;
        }
        entry.customFields.insert(line.left(equals).trimmed(), line.mid(equals + 1));
    }

    QString error;
    if (entry.id.isEmpty()) {
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_currentEntryId = entry.id;
        if (!m_vault.addEntry(entry, &error)) {
            QMessageBox::warning(this, QStringLiteral("Vault"), error);
            return;
        }
    } else if (!m_vault.updateEntry(entry, &error)) {
        QMessageBox::warning(this, QStringLiteral("Vault"), error);
        return;
    }
    if (!persist())
        return;
    refreshEntryTable();
    appendStatus(QStringLiteral("Entry saved."));
    resetActivity();
}

void VaultPage::deleteEntry()
{
    const QString id = selectedEntryId();
    if (id.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete entry"),
                              QStringLiteral("Delete the selected vault entry?"))
        != QMessageBox::Yes)
        return;
    QString error;
    if (!m_vault.removeEntry(id, &error) || !persist()) {
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Vault"), error);
        return;
    }
    m_currentEntryId.clear();
    refreshEntryTable();
    clearEntryEditor();
    appendStatus(QStringLiteral("Entry deleted."));
}

void VaultPage::generateEntryPassword()
{
    GeneratorOptions options;
    options.length = m_generatorLength->value();
    options.uppercase = m_genUpper->isChecked();
    options.lowercase = m_genLower->isChecked();
    options.digits = m_genDigits->isChecked();
    options.symbols = m_genSymbols->isChecked();
    options.excludeAmbiguous = m_genAmbiguous->isChecked();
    QString error;
    const QString password = PasswordGenerator::generate(options, &error);
    if (password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Generator"), error);
        return;
    }
    m_entryPasswordEdit->setText(password);
    m_showPasswordCheck->setChecked(true);
    updateStrength();
    resetActivity();
}

void VaultPage::copyEntryPassword()
{
    if (m_entryPasswordEdit->text().isEmpty())
        return;
    copyToClipboard(m_entryPasswordEdit->text(), QStringLiteral("Password"));
}

void VaultPage::copyEntryUsername()
{
    if (m_usernameEdit->text().isEmpty())
        return;
    copyToClipboard(m_usernameEdit->text(), QStringLiteral("Username"));
}

void VaultPage::updateStrength()
{
    const double bits = PasswordGenerator::estimatedBits(m_entryPasswordEdit->text());
    m_strengthLabel->setText(QStringLiteral("Strength: %1 (%2 bits)")
                                 .arg(PasswordGenerator::strengthLabel(bits))
                                 .arg(bits, 0, 'f', 0));
}

void VaultPage::refreshArchiveTable()
{
    if (!m_vault.isUnlocked())
        return;
    const QString keepId = m_currentArchiveId;
    m_archiveTable->blockSignals(true);
    m_archiveTable->setRowCount(0);
    int selectRow = -1;
    for (const ArchivePasswordRecord &record : m_vault.archivePasswords()) {
        const int row = m_archiveTable->rowCount();
        m_archiveTable->insertRow(row);
        auto *name = new QTableWidgetItem(record.archiveName);
        name->setData(Qt::UserRole, record.id);
        m_archiveTable->setItem(row, 0, name);
        m_archiveTable->setItem(row, 1, new QTableWidgetItem(record.archivePath));
        m_archiveTable->setItem(row, 2, new QTableWidgetItem(record.group));
        m_archiveTable->setItem(row, 3, new QTableWidgetItem(
            record.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        if (record.id == keepId)
            selectRow = row;
    }
    m_archiveTable->blockSignals(false);
    if (selectRow >= 0)
        m_archiveTable->selectRow(selectRow);
}

QString VaultPage::selectedArchiveId() const
{
    const int row = m_archiveTable->currentRow();
    if (row < 0)
        return QString();
    auto *item = m_archiveTable->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void VaultPage::archiveSelectionChanged()
{
    resetActivity();
    const QString id = selectedArchiveId();
    if (id.isEmpty()) {
        clearArchiveEditor();
        return;
    }
    for (const ArchivePasswordRecord &record : m_vault.archivePasswords()) {
        if (record.id == id) {
            fillArchiveEditor(record);
            return;
        }
    }
}

void VaultPage::clearArchiveEditor()
{
    m_currentArchiveId.clear();
    m_archiveNameEdit->clear();
    m_archivePathEdit->clear();
    m_archivePasswordEdit->clear();
    m_archiveGroupEdit->clear();
    m_archiveNoteEdit->clear();
}

void VaultPage::fillArchiveEditor(const ArchivePasswordRecord &record)
{
    m_currentArchiveId = record.id;
    m_archiveNameEdit->setText(record.archiveName);
    m_archivePathEdit->setText(record.archivePath);
    m_archivePasswordEdit->setText(record.password);
    m_archiveGroupEdit->setText(record.group);
    m_archiveNoteEdit->setText(record.note);
}

void VaultPage::newArchivePassword()
{
    m_archiveTable->clearSelection();
    clearArchiveEditor();
    m_archiveNameEdit->setFocus();
    resetActivity();
}

void VaultPage::saveArchivePassword()
{
    if (!m_vault.isUnlocked())
        return;
    if (m_archivePasswordEdit->text().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Archive password"),
                                 QStringLiteral("A password is required."));
        return;
    }
    if (m_archiveNameEdit->text().trimmed().isEmpty()
        && m_archivePathEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Archive password"),
                                 QStringLiteral("Enter an archive name or path."));
        return;
    }

    ArchivePasswordRecord record;
    record.id = m_currentArchiveId;
    record.archiveName = m_archiveNameEdit->text().trimmed();
    record.archivePath = m_archivePathEdit->text().trimmed();
    record.password = m_archivePasswordEdit->text();
    record.group = m_archiveGroupEdit->text().trimmed();
    record.note = m_archiveNoteEdit->text();

    QString error;
    if (record.id.isEmpty()) {
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_currentArchiveId = record.id;
        if (!m_vault.addArchivePassword(record, &error)) {
            QMessageBox::warning(this, QStringLiteral("Vault"), error);
            return;
        }
    } else if (!m_vault.updateArchivePassword(record, &error)) {
        QMessageBox::warning(this, QStringLiteral("Vault"), error);
        return;
    }
    if (!persist())
        return;
    refreshArchiveTable();
    appendStatus(QStringLiteral("Archive password record saved."));
}

void VaultPage::deleteArchivePassword()
{
    const QString id = selectedArchiveId();
    if (id.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete record"),
                              QStringLiteral("Delete the selected archive password record?"))
        != QMessageBox::Yes)
        return;
    QString error;
    if (!m_vault.removeArchivePassword(id, &error) || !persist()) {
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Vault"), error);
        return;
    }
    m_currentArchiveId.clear();
    clearArchiveEditor();
    refreshArchiveTable();
    appendStatus(QStringLiteral("Archive password record deleted."));
}

void VaultPage::browseArchivePath()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select archive"), QFileInfo(m_archivePathEdit->text()).absolutePath(),
        utils::archiveNameFilter());
    if (file.isEmpty())
        return;
    m_archivePathEdit->setText(file);
    if (m_archiveNameEdit->text().isEmpty())
        m_archiveNameEdit->setText(QFileInfo(file).fileName());
}

void VaultPage::copyArchivePassword()
{
    if (m_archivePasswordEdit->text().isEmpty())
        return;
    copyToClipboard(m_archivePasswordEdit->text(), QStringLiteral("Archive password"));
}

void VaultPage::exportBackup()
{
    const QString suggested = QDir::homePath() + QStringLiteral("/eis-vault-backup-")
                            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"))
                            + QStringLiteral(".eisvault");
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export encrypted vault"), suggested,
        QStringLiteral("E.I.S vault (*.eisvault);;All files (*)"));
    if (file.isEmpty())
        return;
    QString error;
    if (!m_vault.exportEncrypted(file, &error)) {
        QMessageBox::warning(this, QStringLiteral("Backup failed"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("Encrypted backup"),
                             QStringLiteral("Encrypted backup written to:\n%1\n\n"
                                            "It remains encrypted with the current vault key.")
                                 .arg(file));
}

void VaultPage::importBackup()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import encrypted vault"), QDir::homePath(),
        QStringLiteral("E.I.S vault (*.eisvault);;All files (*)"));
    if (file.isEmpty())
        return;
    bool ok = false;
    const QString master = QInputDialog::getText(
        this, QStringLiteral("Import vault"), QStringLiteral("Password of the vault being imported:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;
    int imported = 0;
    QString error;
    if (!m_vault.importEncrypted(file, master, &imported, &error)) {
        QMessageBox::warning(this, QStringLiteral("Import failed"), error);
        return;
    }
    refreshEntryTable();
    refreshArchiveTable();
    appendStatus(QStringLiteral("Imported %1 entries from encrypted backup.").arg(imported));
}

void VaultPage::restoreBackup()
{
    if (QMessageBox::warning(
            this, QStringLiteral("Restore vault"),
            QStringLiteral("This replaces every entry in the current vault with the backup. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Restore encrypted vault"), QDir::homePath(),
        QStringLiteral("E.I.S vault (*.eisvault);;All files (*)"));
    if (file.isEmpty())
        return;
    bool ok = false;
    const QString master = QInputDialog::getText(
        this, QStringLiteral("Restore vault"), QStringLiteral("Password of the backup vault:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;
    QString error;
    if (!m_vault.restoreEncrypted(file, master, &error)) {
        QMessageBox::warning(this, QStringLiteral("Restore failed"), error);
        return;
    }
    refreshEntryTable();
    refreshArchiveTable();
    appendStatus(QStringLiteral("Vault restored from encrypted backup."));
}

void VaultPage::copyToClipboard(const QString &text, const QString &what)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text);
    appendStatus(QStringLiteral("%1 copied. Clipboard clears in 30 seconds (best effort).")
                     .arg(what));
    QTimer::singleShot(30000, this, [text]() {
        QClipboard *current = QGuiApplication::clipboard();
        if (current->text() == text)
            current->clear();
    });
    resetActivity();
}

void VaultPage::appendStatus(const QString &text)
{
    m_statusLabel->setText(text);
    emit statusMessage(text);
}

void VaultPage::resetActivity()
{
    if (!m_vault.isUnlocked() || !m_autoLockTimer)
        return;
    m_autoLockTimer->start(m_autoLockSpin->value() * 60 * 1000);
}

void VaultPage::updateAutoLock()
{
    resetActivity();
}

void VaultPage::autoLockTimeout()
{
    if (!m_vault.isUnlocked())
        return;
    m_vault.save(nullptr);
    m_vault.lock();
    setUnlockedUi(false);
    appendStatus(QStringLiteral("Vault locked after inactivity."));
}

bool VaultPage::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::KeyPress:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        resetActivity();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace eis::ui
