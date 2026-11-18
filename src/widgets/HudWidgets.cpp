#include "widgets/HudWidgets.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QPalette>

namespace eis::widgets {

NavButton::NavButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(34);
}

void NavButton::setActive(bool active)
{
    m_active = active;
    setChecked(active);
    update();
}

void NavButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect().adjusted(2, 2, -2, -2);
    QColor bg = m_active ? QColor("#0e2a33") : QColor("#111823");
    if (underMouse() && !m_active)
        bg = QColor("#1a2433");
    p.setBrush(bg);
    p.setPen(QPen(QColor(m_active ? "#39e6ff" : "#1f2a3a"), 1));
    p.drawRoundedRect(r, 6, 6);
    if (m_active) {
        p.setPen(QPen(QColor("#39e6ff"), 2));
        p.drawLine(r.left() + 4, r.center().y(), r.left() + 10, r.center().y());
    }
    p.setPen(QColor(m_active ? "#39e6ff" : "#cfe7f5"));
    p.drawText(r.adjusted(16, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
}

StatCard::StatCard(const QString &label, const QString &value, QWidget *parent)
    : QFrame(parent)
{
    setObjectName("hud");
    setMinimumHeight(70);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(2);
    m_labelLabel = new QLabel(label.toUpper(), this);
    m_labelLabel->setObjectName("subtitle");
    m_valueLabel = new QLabel(value, this);
    m_valueLabel->setStyleSheet("font-size: 18pt; font-weight: 700;");
    layout->addWidget(m_labelLabel);
    layout->addWidget(m_valueLabel);
    layout->addStretch();
}

void StatCard::setValue(const QString &value)
{
    if (m_valueLabel)
        m_valueLabel->setText(value);
}

void StatCard::setAccent(const QString &color)
{
    m_accent = color;
    if (m_valueLabel)
        m_valueLabel->setStyleSheet(QStringLiteral("font-size: 18pt; font-weight: 700; color: %1;").arg(color));
}

CyberProgressBar::CyberProgressBar(QWidget *parent)
    : QProgressBar(parent)
{
    setTextVisible(true);
    setMinimumHeight(16);
}

void CyberProgressBar::paintEvent(QPaintEvent *event)
{
    QProgressBar::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (minimum() == maximum()) {
        // shimmer
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(57, 230, 255, 60));
        p.drawRect(rect());
    }
}

ConsoleLog::ConsoleLog(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFont("Monospace", 9));
}

void ConsoleLog::appendStyled(const QString &level, const QString &text)
{
    const QString color =
        level == QLatin1String("ERROR") ? QStringLiteral("#ff8a80") :
        level == QLatin1String("WARN") ? QStringLiteral("#ffca28") :
        level == QLatin1String("OK") ? QStringLiteral("#69f0ae") :
        level == QLatin1String("DEBUG") ? QStringLiteral("#8aa6b8") :
        QStringLiteral("#cfe7f5");
    appendHtml(QStringLiteral("<span style='color:%1'>[%2]</span> %3")
                   .arg(color, level, text.toHtmlEscaped()));
}

} // namespace eis::widgets

