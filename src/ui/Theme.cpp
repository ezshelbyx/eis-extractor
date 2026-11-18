#include "ui/Theme.h"

#include <QWidget>

namespace eis::ui {

namespace {

QString darkSheet(bool compact, bool reducedMotion)
{
    const int pad = compact ? 6 : 10;
    const int font = compact ? 10 : 11;
    const QString anim = reducedMotion ? QStringLiteral("0ms") : QStringLiteral("140ms");
    return QStringLiteral(R"QSS(
QWidget { background-color: #0b0f14; color: #cfe7f5; font-family: "Noto Sans", "Segoe UI", sans-serif; font-size: %1pt; }
QMainWindow { background-color: #0b0f14; }
QFrame#panel { background-color: #111823; border: 1px solid #1f2a3a; border-radius: 10px; }
QFrame#hud { background-color: #0e1520; border: 1px solid #1a7f9a; border-radius: 8px; }
QLabel#title { color: #39e6ff; font-size: 15pt; font-weight: 700; letter-spacing: 2px; }
QLabel#subtitle { color: #8aa6b8; font-size: 9pt; }
QLabel#section { color: #b388ff; font-weight: 700; letter-spacing: 1px; }
QPushButton { background-color: #121a26; color: #cfe7f5; border: 1px solid #1f2a3a; border-radius: 6px; padding: %2px %3px; }
QPushButton:hover { background-color: #1a2433; border-color: #39e6ff; color: #39e6ff; }
QPushButton:pressed { background-color: #0e1520; }
QPushButton#primary { background-color: #0e2a33; border: 1px solid #39e6ff; color: #39e6ff; font-weight: 600; }
QPushButton#primary:hover { background-color: #123340; }
QPushButton#danger { border-color: #ff5252; color: #ff8a80; }
QPushButton#danger:hover { background-color: #2a1216; }
QPushButton:disabled { color: #4a5a6a; border-color: #1a2230; }
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox {
    background-color: #0e1520; color: #cfe7f5; border: 1px solid #1f2a3a; border-radius: 6px; padding: 4px;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: #39e6ff; }
QComboBox::drop-down { border: none; }
QTabWidget::pane { border: 1px solid #1f2a3a; border-radius: 6px; }
QTabBar::tab { background: #111823; color: #8aa6b8; padding: 6px 12px; border: 1px solid #1f2a3a; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; }
QTabBar::tab:selected { color: #39e6ff; background: #0e1520; border-color: #39e6ff; }
QProgressBar { background-color: #0e1520; border: 1px solid #1f2a3a; border-radius: 4px; text-align: center; color: #cfe7f5; }
QProgressBar::chunk { background-color: #39e6ff; border-radius: 3px; }
QTableWidget, QTreeWidget, QListWidget { background-color: #0e1520; alternate-background-color: #101722; gridline-color: #1f2a3a; border: 1px solid #1f2a3a; border-radius: 6px; }
QHeaderView::section { background-color: #111823; color: #b388ff; padding: 4px; border: none; border-bottom: 1px solid #1f2a3a; font-weight: 600; }
QScrollBar:vertical { background: #0b0f14; width: 10px; }
QScrollBar::handle:vertical { background: #1f2a3a; border-radius: 5px; min-height: 20px; }
QScrollBar::handle:vertical:hover { background: #39e6ff; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QSplitter::handle { background: #1f2a3a; }
QStatusBar { background-color: #0e1520; color: #8aa6b8; }
QToolTip { background-color: #111823; color: #cfe7f5; border: 1px solid #39e6ff; padding: 4px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #1f2a3a; border-radius: 3px; background: #0e1520; }
QCheckBox::indicator:checked { background: #39e6ff; border-color: #39e6ff; }
)QSS").arg(font).arg(pad).arg(pad * 2);
}

QString lightSheet(bool compact, bool reducedMotion)
{
    Q_UNUSED(reducedMotion)
    const int pad = compact ? 6 : 10;
    const int font = compact ? 10 : 11;
    return QStringLiteral(R"QSS(
QWidget { background-color: #f4f7fb; color: #1a2230; font-family: "Noto Sans", "Segoe UI", sans-serif; font-size: %1pt; }
QMainWindow { background-color: #f4f7fb; }
QFrame#panel { background-color: #ffffff; border: 1px solid #d6dee8; border-radius: 10px; }
QFrame#hud { background-color: #ffffff; border: 1px solid #3a9fc4; border-radius: 8px; }
QLabel#title { color: #0a7ea4; font-size: 15pt; font-weight: 700; letter-spacing: 2px; }
QLabel#subtitle { color: #5a6a7a; font-size: 9pt; }
QLabel#section { color: #6a3ab5; font-weight: 700; letter-spacing: 1px; }
QPushButton { background-color: #ffffff; color: #1a2230; border: 1px solid #d6dee8; border-radius: 6px; padding: %2px %3px; }
QPushButton:hover { background-color: #eef4fa; border-color: #3a9fc4; color: #0a7ea4; }
QPushButton#primary { background-color: #e6f4fa; border: 1px solid #3a9fc4; color: #0a7ea4; font-weight: 600; }
QPushButton#danger { border-color: #e53935; color: #c62828; }
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox { background-color: #ffffff; color: #1a2230; border: 1px solid #d6dee8; border-radius: 6px; padding: 4px; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: #3a9fc4; }
QProgressBar { background-color: #eef4fa; border: 1px solid #d6dee8; border-radius: 4px; text-align: center; }
QProgressBar::chunk { background-color: #3a9fc4; border-radius: 3px; }
QTableWidget, QTreeWidget, QListWidget { background-color: #ffffff; alternate-background-color: #f4f7fb; gridline-color: #d6dee8; border: 1px solid #d6dee8; border-radius: 6px; }
QHeaderView::section { background-color: #f4f7fb; color: #6a3ab5; padding: 4px; border: none; border-bottom: 1px solid #d6dee8; font-weight: 600; }
QScrollBar:vertical { background: #f4f7fb; width: 10px; }
QScrollBar::handle:vertical { background: #c8d2dc; border-radius: 5px; min-height: 20px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QToolTip { background-color: #ffffff; color: #1a2230; border: 1px solid #3a9fc4; padding: 4px; }
)QSS").arg(font).arg(pad).arg(pad * 2);
}

} // namespace

QString styleSheet(const QString &name, bool compact, bool reducedMotion)
{
    if (name == QLatin1String("light"))
        return lightSheet(compact, reducedMotion);
    return darkSheet(compact, reducedMotion);
}

void applyTheme(QWidget *root, const QString &name, bool compact, bool reducedMotion)
{
    if (!root)
        return;
    root->setStyleSheet(styleSheet(name, compact, reducedMotion));
}

} // namespace eis::ui
