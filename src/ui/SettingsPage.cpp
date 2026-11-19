#include "ui/SettingsPage.h"

#include "settings/AppSettings.h"
#include "core/Capabilities.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace eis::ui {

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *group = new QGroupBox(QStringLiteral("SETTINGS"), this);
    auto *form = new QFormLayout(group);

    m_themeBox = new QComboBox(group);
    m_themeBox->addItems({QStringLiteral("dark"), QStringLiteral("light")});
    m_themeBox->setCurrentText(AppSettings::instance().theme());
    form->addRow(QStringLiteral("Theme"), m_themeBox);

    m_compactCheck = new QCheckBox(group);
    m_compactCheck->setChecked(AppSettings::instance().compactMode());
    form->addRow(QStringLiteral("Compact mode"), m_compactCheck);

    m_reducedCheck = new QCheckBox(group);
    m_reducedCheck->setChecked(AppSettings::instance().reducedMotion());
    form->addRow(QStringLiteral("Reduced motion"), m_reducedCheck);

    m_concurrencySpin = new QSpinBox(group);
    m_concurrencySpin->setRange(1, 4);
    m_concurrencySpin->setValue(AppSettings::instance().maxConcurrent());
    form->addRow(QStringLiteral("Max concurrent jobs"), m_concurrencySpin);

    m_collisionBox = new QComboBox(group);
    m_collisionBox->addItems({QStringLiteral("rename"), QStringLiteral("skip"), QStringLiteral("overwrite")});
    m_collisionBox->setCurrentText(AppSettings::instance().collisionPolicy());
    form->addRow(QStringLiteral("Default collision policy"), m_collisionBox);

    m_preserveCheck = new QCheckBox(group);
    m_preserveCheck->setChecked(AppSettings::instance().preservePermissions());
    form->addRow(QStringLiteral("Preserve permissions"), m_preserveCheck);

    auto *applyBtn = new QPushButton(QStringLiteral("Apply"), group);
    applyBtn->setObjectName("primary");
    connect(applyBtn, &QPushButton::clicked, this, &SettingsPage::applySettings);
    form->addRow(applyBtn);

    mainLayout->addWidget(group);
    mainLayout->addStretch();

    // Backend capability report
    auto *capLabel = new QLabel(QStringLiteral("Backend capabilities"), this);
    capLabel->setObjectName("section");
    mainLayout->addWidget(capLabel);
    auto *capEdit = new QTextEdit(this);
    capEdit->setReadOnly(true);
    capEdit->setPlainText(CapabilityRegistry::instance().report());
    mainLayout->addWidget(capEdit, 1);
}

void SettingsPage::applySettings()
{
    AppSettings::instance().setTheme(m_themeBox->currentText());
    AppSettings::instance().setCompactMode(m_compactCheck->isChecked());
    AppSettings::instance().setReducedMotion(m_reducedCheck->isChecked());
    AppSettings::instance().setMaxConcurrent(m_concurrencySpin->value());
    AppSettings::instance().setCollisionPolicy(m_collisionBox->currentText());
    AppSettings::instance().setPreservePermissions(m_preserveCheck->isChecked());

}

} // namespace eis::ui
