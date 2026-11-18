#pragma once

#include <QString>

class QWidget;

namespace eis::ui {

void applyTheme(QWidget *root, const QString &name, bool compact, bool reducedMotion);
QString styleSheet(const QString &name, bool compact, bool reducedMotion);

} // namespace eis::ui
