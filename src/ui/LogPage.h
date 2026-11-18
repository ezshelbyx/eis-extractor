#pragma once

#include "core/Types.h"
#include <QWidget>
#include "widgets/HudWidgets.h"

namespace eis::ui {

class LogPage : public QWidget {
    Q_OBJECT
public:
    explicit LogPage(QWidget *parent = nullptr);

public slots:
    void appendEntry(const eis::LogEntry &entry);

private:
    eis::widgets::ConsoleLog *m_log = nullptr;
};

} // namespace eis::ui
