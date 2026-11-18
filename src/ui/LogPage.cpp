#include "ui/LogPage.h"

#include "core/Types.h"
#include "logging/Logger.h"
#include "widgets/HudWidgets.h"

#include <QVBoxLayout>

namespace eis::ui {

LogPage::LogPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    m_log = new eis::widgets::ConsoleLog(this);
    layout->addWidget(m_log);

    for (const eis::LogEntry &e : Logger::instance().history())
        m_log->appendStyled(logLevelName(e.level), e.text);

    connect(&Logger::instance(), &Logger::message, this, &LogPage::appendEntry);
}

void LogPage::appendEntry(const eis::LogEntry &entry)
{
    m_log->appendStyled(logLevelName(entry.level), entry.text);
}

} // namespace eis::ui
