#pragma once

#include "core/Types.h"

#include <QWidget>

class QTableWidget;
class QTableWidgetItem;
class QLineEdit;
class QComboBox;
class QProgressBar;
class QLabel;
class QCheckBox;
class QDragEnterEvent;
class QDropEvent;

namespace eis::ui {

class OperationsPage : public QWidget {
    Q_OBJECT
public:
    explicit OperationsPage(QWidget *parent = nullptr);

    void setArchivePath(const QString &path);
    void refreshJobs(const QVector<eis::JobRecord> &jobs);
    void updateProgress(const QString &jobId, const eis::ProgressInfo &progress);

signals:
    void statusMessage(const QString &text);
    void jobsChanged();

private slots:
    void browseArchive();
    void browseDestination();
    void extractHere();
    void extractToNewFolder();
    void extractToCustom();
    void queueJob();
    void cancelSelected();
    void pauseSelected();
    void resumeSelected();
    void retrySelected();
    void testSelected();
    void useSavedPassword();

private:
    eis::ExtractionOptions buildOptions(bool toNewFolder, bool customDestination);
    QString selectedJobId() const;
    void enqueue(const eis::ExtractionOptions &options);
    void updateJobRow(const eis::JobRecord &record);

    QLineEdit *m_archiveEdit = nullptr;
    QLineEdit *m_destEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QComboBox *m_collisionBox = nullptr;
    QCheckBox *m_preserveCheck = nullptr;
    QTableWidget *m_jobsTable = nullptr;
    QLabel *m_progressLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_etaLabel = nullptr;
    QLabel *m_filesLabel = nullptr;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

} // namespace eis::ui
