#pragma once

#include <QWidget>

class QLineEdit;
class QTextEdit;

namespace eis::ui {

class IntegrityPage : public QWidget {
    Q_OBJECT
public:
    explicit IntegrityPage(QWidget *parent = nullptr);

private slots:
    void browseArchive();
    void runTest();
    void computeSha();

private:
    QLineEdit *m_archiveEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_expectedEdit = nullptr;
    QTextEdit *m_resultEdit = nullptr;
};

} // namespace eis::ui
