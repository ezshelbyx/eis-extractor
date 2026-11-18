#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QVector>

class QStackedWidget;

namespace eis::ui {

class OverviewPage;
class OperationsPage;
class InspectorPage;
class IntegrityPage;
class LogPage;
class SettingsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void switchPage(int index);
    void onJobChanged();

public:
    void openArchive(const QString &path);

private:
    void buildNavigation();
    void applyTheme();

    ::QStackedWidget *m_stack = nullptr;
    ::QVector<::QPushButton*> *m_navButtons = nullptr;
    OverviewPage *m_overview = nullptr;
    OperationsPage *m_operations = nullptr;
    InspectorPage *m_inspector = nullptr;
    IntegrityPage *m_integrity = nullptr;
    LogPage *m_log = nullptr;
    SettingsPage *m_settings = nullptr;
};

} // namespace eis::ui
