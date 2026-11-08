#pragma once

#include "HistoryStore.h"

#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <functional>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class HistoryTree;
class Switch;
class ThemeRow;
class QButtonGroup;
class RequestTab;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    RequestTab *currentTab() const;
    RequestTab *addTab();
    void newTab();
    void closeTab(int index);
    void stepTab(int delta);
    void updateTabTitle(RequestTab *tab);
    void updateTabOverflow();
    void applyToTabs(const std::function<void(RequestTab *)> &fn);

    QString sessionPath() const;
    void saveSession() const;
    bool restoreSession();

    void buildMenus();
    void openHistoryFolder();
    void clearHistory();
    void showShortcuts();
    void openThemePicker();
    void onThemeChanged();

    void recordHistory(const QJsonObject &request, int status, const QString &reason, qint64 ms, qint64 size);
    void openHistoryEntry(int index);
    void rebuildHistory();
    void recolorHistory();
    void applyHistoryFilter();
    void toggleSidebar();
    void focusFilter();

    QTabWidget *m_tabs;
    QToolButton *m_tabList = nullptr;
    bool m_wrap = false;
    bool m_sideBySide = false;

    HistoryStore m_store;
    QList<HistoryEntry> m_entries;
    QWidget *m_sideContainer;
    QWidget *m_sidebar;
    QStackedWidget *m_panels;
    QToolButton *m_historyButton;
    QLineEdit *m_filter;
    QLabel *m_historyTitle;
    HistoryTree *m_historyTree;
    QButtonGroup *m_chips;
    Switch *m_sideSwitch = nullptr;
    Switch *m_wrapSwitch = nullptr;
    ThemeRow *m_themeRow = nullptr;
    QLabel *m_dataCount = nullptr;
};
