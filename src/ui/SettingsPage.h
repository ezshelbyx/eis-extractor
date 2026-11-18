#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

namespace eis::ui {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

private slots:
    void applySettings();

private:
    QComboBox *m_themeBox = nullptr;
    QCheckBox *m_compactCheck = nullptr;
    QCheckBox *m_reducedCheck = nullptr;
    QSpinBox *m_concurrencySpin = nullptr;
    QComboBox *m_collisionBox = nullptr;
    QCheckBox *m_preserveCheck = nullptr;
};

} // namespace eis::ui
