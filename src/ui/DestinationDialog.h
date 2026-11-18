#pragma once

#include <QDialog>
#include <QLineEdit>

namespace eis::ui {

class DestinationDialog : public QDialog {
    Q_OBJECT
public:
    explicit DestinationDialog(const QString &suggested, QWidget *parent = nullptr);
    QString destination() const;

private:
    QLineEdit *m_edit = nullptr;
};

} // namespace eis::ui
