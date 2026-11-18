#pragma once

#include "core/Types.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace eis::ui {

class ProgressWindow : public QDialog {
    Q_OBJECT
public:
    explicit ProgressWindow(QWidget *parent = nullptr);

    void setArchiveCount(int count);
    void setArchiveInfo(int index, const QString &archiveName, const QString &destination);

public slots:
    void onProgress(const eis::ProgressInfo &info);
    void onArchiveFinished(bool ok, const QString &error);
    void onAllFinished(bool ok);

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openDestination();

private:
    void updateStatus(const QString &text, const QString &color);

    QLabel *m_archiveLabel = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_percentLabel = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_filesLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_etaLabel = nullptr;
    QLabel *m_destLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_minBtn = nullptr;
    QPushButton *m_openBtn = nullptr;

    QString m_destination;
    bool m_finished = false;
    bool m_success = false;
};

} // namespace eis::ui
