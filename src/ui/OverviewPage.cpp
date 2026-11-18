#include "ui/OverviewPage.h"

#include "core/Types.h"
#include "widgets/HudWidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QFileInfo>
#include <QVBoxLayout>
#include <algorithm>

namespace eis::ui {

OverviewPage::OverviewPage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("OVERVIEW"), this);
    title->setObjectName("title");
    mainLayout->addWidget(title);

    auto *cardsRow = new QHBoxLayout();
    m_activeCard = new eis::widgets::StatCard(QStringLiteral("Active"), QStringLiteral("0"), this);
    m_queuedCard = new eis::widgets::StatCard(QStringLiteral("Queued"), QStringLiteral("0"), this);
    m_completedCard = new eis::widgets::StatCard(QStringLiteral("Completed"), QStringLiteral("0"), this);
    m_failedCard = new eis::widgets::StatCard(QStringLiteral("Failed"), QStringLiteral("0"), this);
    m_failedCard->setAccent(QStringLiteral("#ff8a80"));
    cardsRow->addWidget(m_activeCard);
    cardsRow->addWidget(m_queuedCard);
    cardsRow->addWidget(m_completedCard);
    cardsRow->addWidget(m_failedCard);
    mainLayout->addLayout(cardsRow);

    auto *recentLabel = new QLabel(QStringLiteral("RECENT ACTIVITY"), this);
    recentLabel->setObjectName("section");
    mainLayout->addWidget(recentLabel);

    m_recentList = new QListWidget(this);
    mainLayout->addWidget(m_recentList, 1);
}

void OverviewPage::refresh(const QVector<eis::JobRecord> &jobs)
{
    int active = 0, queued = 0, completed = 0, failed = 0;
    m_recentList->clear();
    for (const eis::JobRecord &rec : jobs) {
        switch (rec.state) {
        case eis::JobState::Running:
        case eis::JobState::Paused:
            ++active;
            break;
        case eis::JobState::Queued:
            ++queued;
            break;
        case eis::JobState::Completed:
            ++completed;
            break;
        case eis::JobState::Failed:
        case eis::JobState::Cancelled:
            ++failed;
            break;
        }
    }
    m_activeCard->setValue(QString::number(active));
    m_queuedCard->setValue(QString::number(queued));
    m_completedCard->setValue(QString::number(completed));
    m_failedCard->setValue(QString::number(failed));

    // recent: last 20 jobs by queuedAt
    QVector<eis::JobRecord> sorted = jobs;
    std::sort(sorted.begin(), sorted.end(), [](const eis::JobRecord &a, const eis::JobRecord &b) {
        return a.queuedAt > b.queuedAt;
    });
    int count = 0;
    for (const eis::JobRecord &rec : sorted) {
        if (count++ >= 20)
            break;
        m_recentList->addItem(QStringLiteral("%1  %2  %3")
                                  .arg(rec.queuedAt.toString(QStringLiteral("hh:mm:ss")))
                                  .arg(QFileInfo(rec.options.archivePath).fileName())
                                  .arg(jobStateLabel(rec.state)));
    }
}

} // namespace eis::ui

