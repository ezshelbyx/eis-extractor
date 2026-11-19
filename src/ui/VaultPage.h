#pragma once

#include "vault/PasswordVault.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;

namespace eis::ui {

class VaultPage : public QWidget {
    Q_OBJECT
public:
    explicit VaultPage(QWidget *parent = nullptr);

signals:
    void statusMessage(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void browseVaultPath();
    void createVault();
    void unlockVault();
    void lockVault();
    void saveVault();

    void refreshEntryTable();
    void entrySelectionChanged();
    void newEntry();
    void saveEntry();
    void deleteEntry();
    void generateEntryPassword();
    void copyEntryPassword();
    void copyEntryUsername();
    void updateStrength();

    void refreshArchiveTable();
    void archiveSelectionChanged();
    void newArchivePassword();
    void saveArchivePassword();
    void deleteArchivePassword();
    void browseArchivePath();
    void copyArchivePassword();

    void exportBackup();
    void importBackup();
    void restoreBackup();
    void autoLockTimeout();
    void updateAutoLock();

private:
    void buildLockedPage();
    void buildUnlockedPage();
    void setUnlockedUi(bool unlocked);
    void resetActivity();
    bool persist();
    void appendStatus(const QString &text);
    void copyToClipboard(const QString &text, const QString &what);
    void clearEntryEditor();
    void fillEntryEditor(const VaultEntry &entry);
    void clearArchiveEditor();
    void fillArchiveEditor(const ArchivePasswordRecord &record);
    QString selectedEntryId() const;
    QString selectedArchiveId() const;

    PasswordVault &m_vault;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_lockedPage = nullptr;
    QWidget *m_unlockedPage = nullptr;

    QLineEdit *m_pathEdit = nullptr;
    QLineEdit *m_masterEdit = nullptr;
    QLabel *m_lockedInfo = nullptr;

    QLabel *m_vaultPathLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTabWidget *m_tabs = nullptr;

    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_categoryFilter = nullptr;
    QCheckBox *m_favoritesOnly = nullptr;
    QTableWidget *m_entryTable = nullptr;

    QComboBox *m_typeBox = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_entryPasswordEdit = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QLineEdit *m_emailEdit = nullptr;
    QLineEdit *m_categoryEdit = nullptr;
    QLineEdit *m_tagsEdit = nullptr;
    QPlainTextEdit *m_notesEdit = nullptr;
    QPlainTextEdit *m_customFieldsEdit = nullptr;
    QCheckBox *m_favoriteCheck = nullptr;
    QCheckBox *m_showPasswordCheck = nullptr;
    QLabel *m_strengthLabel = nullptr;
    QLabel *m_historyLabel = nullptr;
    QSpinBox *m_generatorLength = nullptr;
    QCheckBox *m_genUpper = nullptr;
    QCheckBox *m_genLower = nullptr;
    QCheckBox *m_genDigits = nullptr;
    QCheckBox *m_genSymbols = nullptr;
    QCheckBox *m_genAmbiguous = nullptr;
    QString m_currentEntryId;

    QTableWidget *m_archiveTable = nullptr;
    QLineEdit *m_archiveNameEdit = nullptr;
    QLineEdit *m_archivePathEdit = nullptr;
    QLineEdit *m_archivePasswordEdit = nullptr;
    QLineEdit *m_archiveGroupEdit = nullptr;
    QLineEdit *m_archiveNoteEdit = nullptr;
    QString m_currentArchiveId;

    QSpinBox *m_autoLockSpin = nullptr;
    QTimer *m_autoLockTimer = nullptr;
};

} // namespace eis::ui
