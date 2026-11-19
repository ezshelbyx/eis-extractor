#pragma once

#include "verification/HashEngine.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QThread;

namespace eis::ui {

class VerificationPage : public QWidget {
    Q_OBJECT
public:
    explicit VerificationPage(QWidget *parent = nullptr);
    ~VerificationPage() override;

signals:
    void statusMessage(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addFiles();
    void removeSelected();
    void calculateHashes();
    void cancelOperation();
    void browseChecksumOutput();
    void generateChecksumFile();
    void browseChecksumFile();
    void verifyChecksumFile();
    void onProgress(int percent, const QString &status);
    void onFinished(bool ok, const QStringList &lines);

private:
    enum class Mode { Hash, Generate, Verify };
    void startWorker(Mode mode);
    void setBusy(bool busy);

    QListWidget *m_files = nullptr;
    QComboBox *m_algorithm = nullptr;
    QPushButton *m_hashButton = nullptr;
    QPushButton *m_generateButton = nullptr;
    QPushButton *m_verifyButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QTextEdit *m_output = nullptr;
    QLineEdit *m_checksumOutputEdit = nullptr;
    QLineEdit *m_checksumFileEdit = nullptr;

    QThread *m_thread = nullptr;
    void *m_worker = nullptr;
};

} // namespace eis::ui
