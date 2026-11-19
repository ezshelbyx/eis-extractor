#pragma once

#include "discimage/DiscImageEngine.h"
#include "widgets/ArchiveTreeModel.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QThread;
class QTreeView;

namespace eis::ui {

class DiscImagePage : public QWidget {
    Q_OBJECT
public:
    explicit DiscImagePage(QWidget *parent = nullptr);
    ~DiscImagePage() override;

signals:
    void statusMessage(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void browseImage();
    void inspectImage();
    void extractImage();
    void mountImage();
    void unmountImage();
    void createIso();
    void calculateChecksum();
    void onWorkerFinished(bool ok, const QString &message);

private:
    void setBusy(bool busy);
    void refreshMounts();

    QLineEdit *m_imageEdit = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_mountLabel = nullptr;
    QTextEdit *m_details = nullptr;
    QTreeView *m_treeView = nullptr;
    eis::widgets::ArchiveTreeModel *m_model = nullptr;
    QPushButton *m_mountButton = nullptr;
    QPushButton *m_unmountButton = nullptr;
    QPushButton *m_inspectButton = nullptr;
    QPushButton *m_extractButton = nullptr;

    DiscImageEngine m_engine;
    DiscImageInfo m_info;
    QThread *m_thread = nullptr;
    void *m_worker = nullptr;
};

} // namespace eis::ui
