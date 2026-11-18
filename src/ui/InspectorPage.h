#pragma once

#include "core/Types.h"
#include "widgets/ArchiveTreeModel.h"

class QLineEdit;
class QTreeView;
class QTextEdit;
class QProgressBar;

#include <QWidget>

namespace eis::ui {

class InspectorPage : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPage(QWidget *parent = nullptr);

    void setArchivePath(const QString &path);

private slots:
    void browseArchive();
    void loadArchive();
    void extractSelected();

private:
    void showDetails(int row);

    QLineEdit *m_archiveEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QTreeView *m_treeView = nullptr;
    eis::widgets::ArchiveTreeModel *m_model = nullptr;
    QTextEdit *m_detailsEdit = nullptr;
    QProgressBar *m_progressBar = nullptr;
    ArchiveListing m_listing;
};

} // namespace eis::ui
