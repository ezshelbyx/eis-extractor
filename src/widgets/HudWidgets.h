#pragma once

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QPlainTextEdit>

namespace eis::widgets {

class NavButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive WRITE setActive)
public:
    explicit NavButton(const QString &text, QWidget *parent = nullptr);
    bool isActive() const { return m_active; }
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_active = false;
};

class StatCard : public QFrame {
    Q_OBJECT
public:
    StatCard(const QString &label, const QString &value, QWidget *parent = nullptr);
    void setValue(const QString &value);
    void setAccent(const QString &color);

private:
    QLabel *m_valueLabel = nullptr;
    QLabel *m_labelLabel = nullptr;
    QString m_accent = QStringLiteral("#39e6ff");
};

class CyberProgressBar : public QProgressBar {
    Q_OBJECT
public:
    explicit CyberProgressBar(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
};

class ConsoleLog : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ConsoleLog(QWidget *parent = nullptr);
    void appendStyled(const QString &level, const QString &text);
};

} // namespace eis::widgets
