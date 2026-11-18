#include "ui/DestinationDialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace eis::ui {

DestinationDialog::DestinationDialog(const QString &suggested, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Choose destination"));
    auto *layout = new QVBoxLayout(this);
    auto *row = new QHBoxLayout();
    m_edit = new QLineEdit(suggested, this);
    row->addWidget(m_edit);
    auto *browseBtn = new QPushButton(QStringLiteral("Browse"), this);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Destination"));
        if (!dir.isEmpty())
            m_edit->setText(dir);
    });
    row->addWidget(browseBtn);
    layout->addLayout(row);
    auto *okBtn = new QPushButton(QStringLiteral("OK"), this);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(okBtn);
}

QString DestinationDialog::destination() const
{
    return m_edit->text().trimmed();
}

} // namespace eis::ui
