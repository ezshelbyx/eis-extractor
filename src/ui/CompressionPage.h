#pragma once

#include "compression/CompressionEngine.h"

#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QListWidget;
class QProgressBar;
class QLabel;
class QPushButton;
class QTextEdit;
class QThread;

namespace eis::ui {

class CompressionPage : public QWidget {
    Q_OBJECT
public:
    explicit CompressionPage(QWidget *parent = nullptr);
    ~CompressionPage() override;

    void setSources(const QStringList &paths);

signals:
    void statusMessage(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void addFiles();
    void addFolder();
    void removeSelected();
    void browseOutput();
    void formatChanged();
    void createArchive();
    void cancelOperation();
    void onProgress(const eis::ProgressInfo &progress);
    void onFinished(const eis::CompressionResult &result);

private:
    CompressionRequest buildRequest() const;
    void setBusy(bool busy);
    void updateFormatCapabilities();
    void appendLog(const QString &text);

    QListWidget *m_sources = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QComboBox *m_formatBox = nullptr;
    QComboBox *m_levelBox = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_encryptHeadersCheck = nullptr;
    QSpinBox *m_volumeSpin = nullptr;
    QCheckBox *m_overwriteCheck = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_currentLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_etaLabel = nullptr;
    QLabel *m_ratioLabel = nullptr;
    QTextEdit *m_log = nullptr;

    QThread *m_thread = nullptr;
    void *m_worker = nullptr;
    bool m_autoOutput = true;
};

} // namespace eis::ui
