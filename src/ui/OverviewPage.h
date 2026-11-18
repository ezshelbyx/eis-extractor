#pragma once

#include "core/Types.h"
#include "widgets/HudWidgets.h"

#include <QListWidget>
#include <QWidget>

namespace eis::ui {

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(QWidget *parent = nullptr);

    void refresh(const QVector<eis::JobRecord> &jobs);

private:
    eis::widgets::StatCard *m_activeCard = nullptr;
    eis::widgets::StatCard *m_queuedCard = nullptr;
    eis::widgets::StatCard *m_completedCard = nullptr;
    eis::widgets::StatCard *m_failedCard = nullptr;
    QListWidget *m_recentList = nullptr;
};

} // namespace eis::ui
