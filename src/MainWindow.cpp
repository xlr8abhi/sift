#include "MainWindow.h"
#include "DocTabBar.h"
#include "HistoryList.h"
#include "SettingsWidgets.h"
#include "Style.h"

#include "RequestTab.h"
#include "AppFont.h"
#include "SideRail.h"
#include "Theme.h"
#include "ThemePicker.h"

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkRequest>
#include <QPalette>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

namespace {

QString shortUrl(QString url)
{
    const int scheme = url.indexOf("://");
    if (scheme >= 0)
        url.remove(0, scheme + 3);
    return url;
}

// "2m ago", "1h 4m ago"; any other day just shows the clock time (the day heading says the rest)
QString historyAgo(const QDateTime &time)
{
    if (time.date() != QDate::currentDate())
        return time.toString("HH:mm");
    const qint64 secs = qMax<qint64>(0, time.secsTo(QDateTime::currentDateTime()));
    if (secs < 60)
        return QObject::tr("now");
    const qint64 mins = secs / 60;
    if (mins < 60)
        return QObject::tr("%1m ago").arg(mins);
    return mins % 60 ? QObject::tr("%1h %2m ago").arg(mins / 60).arg(mins % 60) : QObject::tr("%1h ago").arg(mins / 60);
}

QString historyMeta(const HistoryEntry &entry)
{
    QStringList parts{historyAgo(entry.time)};
    const bool unsent = entry.status == 0 && entry.reason.isEmpty();
    if (unsent) {
        parts << QObject::tr("not sent");
    } else {
        if (entry.status != 200 && !entry.reason.isEmpty() && entry.status != 0)
            parts << entry.reason;
        else if (entry.status == 0)
            parts << entry.reason;
        if (entry.ms > 0)
            parts << (entry.ms >= 1000 ? QObject::tr("%1s").arg(entry.ms / 1000.0, 0, 'f', 1)
                                       : QObject::tr("%1ms").arg(entry.ms));
    }
    const QString query = QUrl(entry.request.value("url").toString(), QUrl::TolerantMode).query();
    if (!query.isEmpty()) {
        const int n = query.count('&') + 1;
        parts << (n == 1 ? QObject::tr("1 param") : QObject::tr("%1 params").arg(n));
    }
    return parts.join(QStringLiteral(" · "));
}

QString historyDay(const QDate &date)
{
    if (date == QDate::currentDate())
        return QObject::tr("Today");
    if (date == QDate::currentDate().addDays(-1))
        return QObject::tr("Yesterday");
    return date.toString("d MMM yyyy");
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/history")
{
    setWindowTitle("sift");
    resize(1000, 700);

    m_tabs = new DocTabWidget;
    m_tabs->setObjectName("docTabs");
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    m_tabs->tabBar()->installEventFilter(this);

    auto *plus = new QToolButton;
    plus->setObjectName("newTab");
    plus->setText("+");
    plus->setAutoRaise(true);
    plus->setToolTip(tr("New tab (Ctrl+T)"));

    // Overflow list: appears only when the tabs no longer fit, lists every tab
    m_tabList = new QToolButton;
    m_tabList->setObjectName("tabList");
    m_tabList->setPopupMode(QToolButton::InstantPopup);
    m_tabList->setToolTip(tr("All tabs"));
    m_tabList->setVisible(false);
    auto *tabMenu = new QMenu(m_tabList);
    m_tabList->setMenu(tabMenu);
    connect(tabMenu, &QMenu::aboutToShow, this, [this, tabMenu] {
        qDeleteAll(tabMenu->actions());
        tabMenu->clear();

        // One scrolling list instead of one menu entry per tab, so the menu has a maximum
        // height and opens below its button instead of covering it.
        auto *list = new QWidget;
        auto *listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(0, 0, 0, 0);
        listLayout->setSpacing(0);
        TabListRow *currentRow = nullptr;
        for (int i = 0; i < m_tabs->count(); ++i) {
            auto *rowWidget = new TabListRow(m_tabs->tabBar()->tabData(i).toMap(), i == m_tabs->currentIndex());
            if (i == m_tabs->currentIndex())
                currentRow = rowWidget;
            connect(rowWidget, &TabListRow::clicked, this, [this, tabMenu, i] {
                m_tabs->setCurrentIndex(i);
                tabMenu->close();
            });
            listLayout->addWidget(rowWidget);
        }
        list->adjustSize();

        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setWidget(list);
        scroll->setWidgetResizable(true);
        const int maxHeight = qMin(360, screen() ? screen()->availableGeometry().height() / 2 : 360);
        scroll->setFixedSize(list->sizeHint().width() + 14, qMin(list->sizeHint().height() + 4, maxHeight));

        auto *action = new QWidgetAction(tabMenu);
        action->setDefaultWidget(scroll);
        tabMenu->addAction(action);
        if (currentRow)
            QTimer::singleShot(0, scroll, [scroll, currentRow] { scroll->ensureWidgetVisible(currentRow, 0, 24); });
    });

    auto *corner = new QWidget;
    auto *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(0);
    cornerLayout->addWidget(plus);
    cornerLayout->addWidget(m_tabList);
    m_tabs->setCornerWidget(corner, Qt::TopRightCorner);

    connect(plus, &QToolButton::clicked, this, &MainWindow::newTab);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0)
            newTab();
    });

    m_historyTitle = new QLabel;
    m_historyTitle->setObjectName("panelTitle");
    m_filter = new QLineEdit;
    m_filter->setObjectName("filter");
    m_filter->setPlaceholderText(tr("Filter…  Ctrl+K"));
    m_filter->setClearButtonEnabled(true);
    m_filter->installEventFilter(this);
    m_historyTree = new HistoryTree;
    m_historyTree->setColumnCount(1);
    m_historyTree->setHeaderHidden(true);
    m_historyTree->setRootIsDecorated(false);
    m_historyTree->setUniformRowHeights(false);
    m_historyTree->setItemDelegate(new HistoryDelegate(m_historyTree));
    m_historyTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyTree->header()->setStretchLastSection(true);

    m_chips = new QButtonGroup(this);
    m_chips->setExclusive(true);
    auto *chipRow = new QHBoxLayout;
    chipRow->setContentsMargins(10, 0, 10, 8);
    chipRow->setSpacing(6);
    const QStringList chipNames = {tr("All"), tr("Errors"), QStringLiteral("GET"), QStringLiteral("POST"), tr("Today")};
    for (int i = 0; i < chipNames.size(); ++i) {
        auto *chip = new QToolButton;
        chip->setObjectName("chip");
        chip->setText(chipNames.at(i));
        chip->setCheckable(true);
        chip->setChecked(i == 0);
        chip->setFocusPolicy(Qt::NoFocus);
        m_chips->addButton(chip, i);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);

    m_sidebar = new QWidget;
    auto *sideLayout = new QVBoxLayout(m_sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 8, 0);
    titleRow->addWidget(m_historyTitle, 1);
    sideLayout->addLayout(titleRow);
    auto *filterWrap = new QWidget;
    auto *filterLayout = new QHBoxLayout(filterWrap);
    filterLayout->setContentsMargins(10, 0, 10, 8);
    filterLayout->addWidget(m_filter);
    sideLayout->addWidget(filterWrap);
    sideLayout->addLayout(chipRow);
    sideLayout->addWidget(m_historyTree, 1);

    // Settings panel: same slot as History, chosen from the rail.
    auto *settingsPage = new QWidget;
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    auto *settingsTitle = new QLabel(tr("Settings"));
    settingsTitle->setObjectName("panelTitle");
    settingsLayout->addWidget(settingsTitle);

    // Rows: a label on the left, the control on the right
    auto row = [](const QString &title, QWidget *control) {
        auto *label = new QLabel(title);
        label->setObjectName("fieldLabel");
        auto *line = new QHBoxLayout;
        line->addWidget(label, 1);
        line->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
        return line;
    };
    auto note = [](const QString &text) {
        auto *note = new QLabel(text);
        note->setObjectName("meta");
        note->setWordWrap(true);
        return note;
    };
    auto link = [this](const QString &text, const char *name, auto slot) {
        auto *button = new QToolButton;
        button->setObjectName(name);
        button->setText(text);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QToolButton::clicked, this, slot);
        return button;
    };
    auto section = [settingsLayout](const QString &title) {
        auto *label = new QLabel(title);
        label->setObjectName("sectionLabel");
        settingsLayout->addWidget(label);
    };
    auto body = [settingsLayout] {
        auto *box = new QVBoxLayout;
        box->setContentsMargins(12, 4, 12, 12);
        box->setSpacing(10);
        settingsLayout->addLayout(box);
        return box;
    };

    // Appearance
    section(tr("APPEARANCE"));
    auto *appearance = body();

    m_themeRow = new ThemeRow;
    connect(m_themeRow, &QAbstractButton::clicked, this, &MainWindow::openThemePicker);
    auto *themeLabel = new QLabel(tr("Theme"));
    themeLabel->setObjectName("fieldLabel");
    auto *themeKey = new QLabel(tr("Alt+T"));
    themeKey->setObjectName("meta");
    auto *themeHead = new QHBoxLayout;
    themeHead->addWidget(themeLabel, 1);
    themeHead->addWidget(themeKey);
    auto *themeGroup = new QVBoxLayout;
    themeGroup->setSpacing(6);
    themeGroup->addLayout(themeHead);
    themeGroup->addWidget(m_themeRow);
    appearance->addLayout(themeGroup);

    // Font size: a stepper over the same sizes the old drop-down offered
    static const QList<int> sizes = {8, 9, 10, 11, 12, 13, 14, 16, 18, 20};
    auto *stepper = new QFrame;
    stepper->setObjectName("stepper");
    auto *stepperLayout = new QHBoxLayout(stepper);
    stepperLayout->setContentsMargins(2, 2, 2, 2);
    stepperLayout->setSpacing(0);
    auto *smaller = new QToolButton;
    smaller->setText(QString(QChar(0x2212)));
    smaller->setAutoRepeat(true);
    smaller->setToolTip(tr("Smaller"));
    auto *larger = new QToolButton;
    larger->setText("+");
    larger->setAutoRepeat(true);
    larger->setToolTip(tr("Larger"));
    auto *sizeValue = new QLabel;
    sizeValue->setAlignment(Qt::AlignCenter);
    sizeValue->setMinimumWidth(52);
    stepperLayout->addWidget(smaller);
    stepperLayout->addWidget(sizeValue);
    stepperLayout->addWidget(larger);
    auto showSize = [sizeValue] { sizeValue->setText(tr("%1 pt").arg(AppFont::effectiveSize(), 0, 'f', AppFont::effectiveSize() == int(AppFont::effectiveSize()) ? 0 : 1)); };
    showSize();
    connect(smaller, &QToolButton::clicked, this, [showSize] {
        const qreal now = AppFont::effectiveSize();
        for (int i = sizes.size() - 1; i >= 0; --i) {
            if (sizes.at(i) < now - 0.01) {
                AppFont::setSize(sizes.at(i));
                break;
            }
        }
        showSize();
    });
    connect(larger, &QToolButton::clicked, this, [showSize] {
        const qreal now = AppFont::effectiveSize();
        for (int size : sizes) {
            if (size > now + 0.01) {
                AppFont::setSize(size);
                break;
            }
        }
        showSize();
    });
    appearance->addLayout(row(tr("Font size"), stepper));

    // Font weight
    static const QList<int> weights = {0, QFont::Light, QFont::Normal, QFont::Bold};
    auto *weightLabel = new QLabel(tr("Font weight"));
    weightLabel->setObjectName("fieldLabel");
    auto *weightBar = new Segmented({tr("Default"), tr("Light"), tr("Normal"), tr("Bold")});
    weightBar->setCurrent(weights.indexOf(AppFont::savedWeight())); // medium / semi-bold: none selected
    connect(weightBar, &Segmented::activated, this, [](int index) { AppFont::setWeight(weights.at(index)); });
    auto *weightGroup = new QVBoxLayout;
    weightGroup->setSpacing(6);
    weightGroup->addWidget(weightLabel);
    weightGroup->addWidget(weightBar);
    appearance->addLayout(weightGroup);

    appearance->addWidget(new FontPreview);
    appearance->addWidget(note(tr("Preview. Size and weight apply to the whole app, including the code views.")));

    // Layout
    section(tr("LAYOUT"));
    auto *layoutBody = body();
    m_sideSwitch = new Switch;
    layoutBody->addLayout(row(tr("Side by side"), m_sideSwitch));
    layoutBody->addWidget(note(tr("Request and response next to each other.  Ctrl+\\")));
    m_wrapSwitch = new Switch;
    layoutBody->addLayout(row(tr("Wrap long lines"), m_wrapSwitch));
    layoutBody->addWidget(note(tr("Alt+Z")));

    // Data
    section(tr("DATA"));
    auto *dataBody = body();
    m_dataCount = new QLabel;
    m_dataCount->setObjectName("meta");
    dataBody->addLayout(row(tr("History"), m_dataCount));
    auto *dataLinks = new QHBoxLayout;
    dataLinks->addWidget(link(tr("Open folder"), "linkButton", &MainWindow::openHistoryFolder));
    dataLinks->addStretch(1);
    dataBody->addLayout(dataLinks);

    // Help
    section(tr("HELP"));
    auto *helpBody = body();
    auto *helpRow = new QHBoxLayout;
    helpRow->addWidget(link(tr("Keyboard shortcuts"), "linkButton", &MainWindow::showShortcuts));
    helpRow->addStretch(1);
    auto *helpKey = new QLabel(tr("F1"));
    helpKey->setObjectName("meta");
    helpRow->addWidget(helpKey);
    helpBody->addLayout(helpRow);
    auto *resetButton = link(tr("Reset font to defaults"), "quietLink", [weightBar, showSize] {
        AppFont::setSize(0);
        AppFont::setWeight(0);
        weightBar->setCurrent(0);
        showSize();
    });
    helpBody->addWidget(resetButton, 0, Qt::AlignLeft);

    connect(&ThemeManager::instance(), &ThemeManager::changed, m_themeRow, qOverload<>(&QWidget::update));

    settingsLayout->addStretch(1);

    // The panel can be taller than a small window: let it scroll
    auto *settingsScroll = new QScrollArea;
    settingsScroll->setObjectName("settingsScroll");
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsPage->setObjectName("settingsBody");
    settingsScroll->setWidget(settingsPage);

    m_panels = new QStackedWidget;
    m_panels->addWidget(m_sidebar);
    m_panels->addWidget(settingsScroll);

    auto *rail = new SideRail;
    m_historyButton = rail->addButton(SideRail::Icon::History, tr("History"));
    m_historyButton->setChecked(true);
    connect(m_historyButton, &QToolButton::clicked, this, &MainWindow::focusFilter);

    auto *settingsButton = rail->addBottomButton(SideRail::Icon::Settings, tr("Settings"));
    connect(settingsButton, &QToolButton::clicked, this, [this] {
        m_sideContainer->show();
        m_panels->setCurrentIndex(1);
    });

    m_sideContainer = new QWidget;
    auto *containerLayout = new QHBoxLayout(m_sideContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);
    containerLayout->addWidget(rail);
    containerLayout->addWidget(m_panels, 1);

    auto *mainSplit = new QSplitter(Qt::Horizontal);
    mainSplit->addWidget(m_sideContainer);
    mainSplit->addWidget(m_tabs);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setSizes({302, 700});
    mainSplit->setChildrenCollapsible(false);

    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->addWidget(mainSplit);
    setCentralWidget(central);

    auto *hint = new QLabel(tr("Ctrl+Enter send  ·  Ctrl+L url  ·  F1 help"));
    statusBar()->addPermanentWidget(hint);
    statusBar()->setSizeGripEnabled(false);

    connect(m_filter, &QLineEdit::textChanged, this, &MainWindow::applyHistoryFilter);
    connect(m_chips, &QButtonGroup::idToggled, this, [this](int, bool on) {
        if (on)
            applyHistoryFilter();
    });
    connect(m_historyTree, &HistoryTree::removeRequested, this, [this](int index) {
        if (index < 0 || index >= m_entries.size())
            return;
        m_store.remove(m_entries.at(index).file);
        m_entries.removeAt(index);
        rebuildHistory();
    });
    connect(m_historyTree, &HistoryTree::tick, this, [this] {
        for (int i = 0; i < m_historyTree->topLevelItemCount(); ++i) {
            auto *item = m_historyTree->topLevelItem(i);
            const int index = item->data(0, Qt::UserRole).toInt();
            if (index >= 0 && index < m_entries.size())
                item->setData(0, HistMeta, historyMeta(m_entries.at(index)));
        }
        m_historyTree->viewport()->update();
    });
    auto openEntry = [this](QTreeWidgetItem *item) {
        if (item)
            openHistoryEntry(item->data(0, Qt::UserRole).toInt());
    };
    connect(m_historyTree, &QTreeWidget::itemClicked, this, openEntry);
    connect(m_historyTree, &HistoryTree::enterPressed, this, openEntry);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &MainWindow::onThemeChanged);

    m_entries = m_store.loadAll();
    rebuildHistory();

    if (!restoreSession())
        addTab();
    currentTab()->focusUrl();
    buildMenus();
    // Restored tabs are hidden and already styled; undo that in the background so the first theme
    // change does not have to (it would take seconds with many tabs).
    QTimer::singleShot(400, this, [] { Style::deferHiddenWidgetsInBackground(); });
}

QString MainWindow::sessionPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/session.json";
}

void MainWindow::saveSession() const
{
    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *tab = qobject_cast<RequestTab *>(m_tabs->widget(i)))
            tabs.append(tab->requestJson());
    }
    QJsonObject root;
    root["current"] = m_tabs->currentIndex();
    root["tabs"] = tabs;

    QDir().mkpath(QFileInfo(sessionPath()).absolutePath());
    QSaveFile file(sessionPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

bool MainWindow::restoreSession()
{
    QFile file(sessionPath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray tabs = root.value("tabs").toArray();
    if (tabs.isEmpty())
        return false;

    for (const QJsonValue &value : tabs)
        addTab()->loadRequest(value.toObject());
    m_tabs->setCurrentIndex(qBound(0, root.value("current").toInt(), m_tabs->count() - 1));
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSession();
    QMainWindow::closeEvent(event);
}

RequestTab *MainWindow::currentTab() const
{
    return qobject_cast<RequestTab *>(m_tabs->currentWidget());
}

RequestTab *MainWindow::addTab()
{
    auto *tab = new RequestTab;
    tab->setWrap(m_wrap);
    tab->setSideBySide(m_sideBySide);
    connect(tab, &RequestTab::wrapToggled, this, [this](bool on) { m_wrapSwitch->setChecked(on); });

    connect(tab, &RequestTab::completed, this,
            [this](const QJsonObject &request, int status, const QString &reason, qint64 ms, qint64 size) {
                recordHistory(request, status, reason, ms, size);
            });
    connect(tab, &RequestTab::summaryChanged, this, [this, tab] { updateTabTitle(tab); });

    const int index = m_tabs->addTab(tab, tr("New request"));
    m_tabs->setCurrentIndex(index);
    updateTabTitle(tab);
    return tab;
}

void MainWindow::newTab()
{
    addTab()->focusUrl();
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabs->count())
        return;
    if (m_tabs->count() == 1)
        addTab()->focusUrl();
    QWidget *tab = m_tabs->widget(index);
    m_tabs->removeTab(index);
    tab->deleteLater();
    updateTabOverflow();
}

void MainWindow::stepTab(int delta)
{
    const int n = m_tabs->count();
    if (n > 1)
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + n) % n);
}

void MainWindow::updateTabTitle(RequestTab *tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0)
        return;

    QTabBar *bar = m_tabs->tabBar();
    const QString url = tab->url();
    if (url.isEmpty()) {
        const QString title = tr("New request");
        m_tabs->setTabText(index, title);
        m_tabs->setTabToolTip(index, QString());
        bar->setTabData(index, DocTab::data(tab->method(), title, QString(), true));
    } else {
        // The tab bar paints the badge and title itself from this data
        QString rest = shortUrl(url).section('?', 0, 0);
        const int slash = rest.indexOf('/');
        const QString host = slash < 0 ? rest : rest.left(slash);
        const QString path = slash < 0 ? QString() : rest.mid(slash);
        m_tabs->setTabText(index, tab->method() + "  " + rest);
        m_tabs->setTabToolTip(index, url);
        bar->setTabData(index, DocTab::data(tab->method(), host, path, false));
    }
    bar->update();
    updateTabOverflow();
}

void MainWindow::updateTabOverflow()
{
    auto *bar = qobject_cast<DocTabBar *>(m_tabs->tabBar());
    if (!bar)
        return;
    int total = 0;
    for (int i = 0; i < bar->count(); ++i)
        total += bar->preferredWidth(i);
    // The corner widget (+ and the list button) takes room from the bar too
    const int room = m_tabs->width() - (m_tabs->cornerWidget() ? m_tabs->cornerWidget()->sizeHint().width() : 0);
    const bool overflow = total > room;
    m_tabList->setVisible(overflow);
    m_tabList->setText(QString::number(bar->count()));
}

void MainWindow::applyToTabs(const std::function<void(RequestTab *)> &fn)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *tab = qobject_cast<RequestTab *>(m_tabs->widget(i)))
            fn(tab);
    }
}

void MainWindow::onThemeChanged()
{
    recolorHistory();
    // Tab labels are painted from the theme while painting, so a repaint is all they need
    m_tabs->tabBar()->update();
}

void MainWindow::openThemePicker()
{
    ThemePicker picker(this);
    picker.exec();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tabs->tabBar() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &MainWindow::updateTabOverflow);

    if (watched == m_tabs->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::MiddleButton) {
            const int index = m_tabs->tabBar()->tabAt(mouse->position().toPoint());
            if (index >= 0) {
                closeTab(index);
                return true;
            }
        }
    }

    if (watched == m_filter && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up || key == Qt::Key_Return || key == Qt::Key_Enter) {
            QTreeWidgetItem *target = m_historyTree->currentItem();
            if (!target || target->isHidden()) {
                target = nullptr;
                for (int i = 0; i < m_historyTree->topLevelItemCount() && !target; ++i) {
                    if (!m_historyTree->topLevelItem(i)->isHidden())
                        target = m_historyTree->topLevelItem(i);
                }
            }
            if (!target)
                return true;
            m_historyTree->setCurrentItem(target);
            m_historyTree->setFocus();
            if (key == Qt::Key_Return || key == Qt::Key_Enter)
                openHistoryEntry(target->data(0, Qt::UserRole).toInt());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::buildMenus()
{
    auto add = [this](QMenu *menu, const QString &text, const QList<QKeySequence> &keys, auto slot) {
        QAction *action = menu->addAction(text);
        if (!keys.isEmpty())
            action->setShortcuts(keys);
        addAction(action);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    auto seq = [](const QString &s) { return QList<QKeySequence>{QKeySequence(s)}; };
    auto stdKey = [](QKeySequence::StandardKey k) { return QList<QKeySequence>{QKeySequence(k)}; };

    QMenu *file = menuBar()->addMenu(tr("&File"));
    add(file, tr("New tab"), seq("Ctrl+T"), &MainWindow::newTab);
    add(file, tr("Close tab"), stdKey(QKeySequence::Close), [this] { closeTab(m_tabs->currentIndex()); });
    file->addSeparator();
    add(file, tr("Open history folder"), {}, &MainWindow::openHistoryFolder);
    add(file, tr("Clear history…"), {}, &MainWindow::clearHistory);
    file->addSeparator();
    add(file, tr("&Quit"), stdKey(QKeySequence::Quit), [] { QApplication::quit(); });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    add(view, tr("Next tab"), {QKeySequence("Ctrl+PgDown"), QKeySequence("Ctrl+Tab")}, [this] { stepTab(1); });
    add(view, tr("Previous tab"), {QKeySequence("Ctrl+PgUp"), QKeySequence("Ctrl+Shift+Tab")},
        [this] { stepTab(-1); });
    view->addSeparator();
    add(view, tr("Toggle sidebar"), seq("Ctrl+B"), &MainWindow::toggleSidebar);
    add(view, tr("Filter history"), seq("Ctrl+K"), &MainWindow::focusFilter);

    QAction *sideBySide = view->addAction(tr("Side by side"));
    sideBySide->setShortcut(QKeySequence("Ctrl+\\"));
    sideBySide->setCheckable(true);
    addAction(sideBySide);
    connect(sideBySide, &QAction::toggled, this, [this](bool on) {
        m_sideBySide = on;
        applyToTabs([on](RequestTab *tab) { tab->setSideBySide(on); });
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
        settings.setValue("side_by_side", on);
    });
    connect(sideBySide, &QAction::toggled, m_sideSwitch, &QAbstractButton::setChecked);
    connect(m_sideSwitch, &QAbstractButton::toggled, sideBySide, &QAction::setChecked);
    view->addSeparator();

    add(view, tr("Response: Pretty"), seq("Alt+1"), [this] { if (auto *t = currentTab()) t->showResponseView(0); });
    add(view, tr("Response: Raw"), seq("Alt+2"), [this] { if (auto *t = currentTab()) t->showResponseView(1); });
    add(view, tr("Response: Headers"), seq("Alt+3"), [this] { if (auto *t = currentTab()) t->showResponseView(2); });
    add(view, tr("Response: Render (HTML)"), seq("Alt+4"), [this] { if (auto *t = currentTab()) t->showResponseView(3); });
    add(view, tr("Search in response"), stdKey(QKeySequence::Find), [this] { if (auto *t = currentTab()) t->openSearch(); });
    add(view, tr("Copy response"), seq("Ctrl+Shift+C"), [this] { if (auto *t = currentTab()) t->copyResponse(); });
    add(view, tr("Copy JSON path of the current line"), seq("Ctrl+Alt+C"), [this] { if (auto *t = currentTab()) t->copyPath(); });

    QAction *wrap = view->addAction(tr("Wrap lines"));
    wrap->setShortcut(QKeySequence("Alt+Z"));
    wrap->setCheckable(true);
    addAction(wrap);
    connect(wrap, &QAction::toggled, this, [this](bool on) {
        m_wrap = on;
        applyToTabs([on](RequestTab *tab) { tab->setWrap(on); });
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
        settings.setValue("wrap", on);
    });
    connect(wrap, &QAction::toggled, m_wrapSwitch, &QAbstractButton::setChecked);
    connect(m_wrapSwitch, &QAbstractButton::toggled, wrap, &QAction::setChecked);
    view->addSeparator();
    add(view, tr("Theme…"), seq("Alt+T"), &MainWindow::openThemePicker);
    view->addSeparator();

    QAction *bar = view->addAction(tr("Show menu bar in window"));
    bar->setShortcut(QKeySequence("Ctrl+M"));
    bar->setCheckable(true);
    bar->setToolTip(tr("Off: menus live in the Plasma Global Menu. On: menus appear inside this window."));
    addAction(bar);
    connect(bar, &QAction::toggled, this, [this](bool inWindow) {
        menuBar()->setNativeMenuBar(!inWindow);
        menuBar()->setVisible(true);
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
        settings.setValue("menubar_in_window", inWindow);
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    add(help, tr("Keyboard shortcuts"), stdKey(QKeySequence::HelpContents), &MainWindow::showShortcuts);
    add(help, tr("About sift"), {}, [this] {
        QMessageBox::about(this, tr("About sift"),
                           tr("<b>sift</b><br>A small native API client.<br>Qt %1, no web engine, no accounts, no telemetry.")
                               .arg(QString::fromLatin1(qVersion())));
    });

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
    sideBySide->setChecked(settings.value("side_by_side", false).toBool());
    wrap->setChecked(settings.value("wrap", false).toBool());
    const bool inWindow = settings.value("menubar_in_window", false).toBool();
    menuBar()->setNativeMenuBar(!inWindow);
    menuBar()->setVisible(true);
    bar->setChecked(inWindow);
}

void MainWindow::openHistoryFolder()
{
    QDir().mkpath(m_store.dir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_store.dir()));
}

void MainWindow::clearHistory()
{
    const int count = m_entries.size();
    if (count == 0) {
        QMessageBox::information(this, tr("Clear history"), tr("The history is already empty."));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Clear history"),
        tr("Delete %n saved request file(s) in\n%1\n\nThis cannot be undone.", nullptr, count).arg(m_store.dir()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    m_store.clearAll();
    m_entries.clear();
    rebuildHistory();
}

void MainWindow::showShortcuts()
{
    static const QList<QPair<QString, QString>> rows = {
        {"Send request", "Ctrl+Enter"},
        {"Focus URL", "Ctrl+L"},
        {"New tab / close tab", "Ctrl+T / Ctrl+W"},
        {"Next / previous tab", "Ctrl+Tab / Ctrl+Shift+Tab"},
        {"Request: Preview / Params / Headers / Body", "Ctrl+1 / 2 / 3 / 4"},
        {"Copy the previewed request (in Preview)", "C"},
        {"Response: Pretty / Raw / Headers / Render", "Alt+1 / 2 / 3 / 4"},
        {"Search in response", "Ctrl+F"},
        {"Copy the whole response / the JSON path of the current line", "Ctrl+Shift+C / Ctrl+Alt+C"},
        {"Copy the JSON path of a line in Pretty", "Ctrl+click the line"},
        {"Copy the value of one header (Headers, Table view)", "Ctrl+click the row"},
        {"Next / previous match", "Enter / Shift+Enter"},
        {"Close search / cancel the running request", "Esc"},
        {"Wrap lines", "Alt+Z"},
        {"Toggle sidebar", "Ctrl+B"},
        {"Side by side or stacked", "Ctrl+\\"},
        {"Filter history", "Ctrl+K"},
        {"Open history item (in a new tab if this one is in use)", "Enter or click"},
        {"Open history item in a new tab", "Shift+Enter or Shift+click"},
        {"Theme picker", "Alt+T"},
        {"Menus in window or Global Menu", "Ctrl+M"},
        {"Keyboard shortcuts", "F1"},
        {"Quit", "Ctrl+Q"},
    };
    QString html = "<table cellpadding='4'>";
    for (const auto &row : rows)
        html += QString("<tr><td>%1</td><td><code>%2</code></td></tr>").arg(row.first, row.second);
    html += "</table>";
    QMessageBox::information(this, tr("Keyboard shortcuts"), html);
}

void MainWindow::recordHistory(const QJsonObject &request, int status, const QString &reason, qint64 ms, qint64 size)
{
    HistoryEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.request = request;
    entry.status = status;
    entry.reason = reason;
    entry.ms = ms;
    entry.size = size;

    if (!m_entries.isEmpty() && m_entries.first().request == entry.request) {
        entry.file = m_entries.first().file;
        m_entries.removeFirst();
    }
    if (!m_store.save(entry))
        qWarning("sift: could not save history to %s", qPrintable(m_store.dir()));
    m_entries.prepend(entry);
    rebuildHistory();
}

void MainWindow::openHistoryEntry(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    const bool inNewTab = QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
    // A history item replaces the current tab only when that tab is still blank;
    // otherwise it opens in a new tab so nothing the user typed is overwritten.
    RequestTab *tab = inNewTab || !currentTab() || !currentTab()->isBlank() ? addTab() : currentTab();
    tab->loadRequest(m_entries.at(index).request);
    updateTabTitle(tab);
}

void MainWindow::rebuildHistory()
{
    m_historyTree->clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const HistoryEntry &entry = m_entries.at(i);
        const QString method = entry.request.value("method").toString();
        const QString url = entry.request.value("url").toString();

        // Split "host/path" for display; the query is summarised as a param count
        QString rest = shortUrl(url);
        rest = rest.section('?', 0, 0);
        const int slash = rest.indexOf('/');
        const QString host = slash < 0 ? rest : rest.left(slash);
        const QString path = slash < 0 ? QString() : rest.mid(slash);

        const bool unsent = entry.status == 0 && entry.reason.isEmpty();
        const QString status = unsent ? QStringLiteral("—") : entry.status ? QString::number(entry.status) : tr("ERR");
        const int kind = unsent ? 3 : (entry.status >= 400 || !entry.status) ? 2 : entry.status >= 300 ? 1 : 0;

        auto *item = new QTreeWidgetItem;
        item->setData(0, Qt::UserRole, i);
        item->setData(0, HistMethod, method);
        item->setData(0, HistHost, host);
        item->setData(0, HistPath, path);
        item->setData(0, HistMeta, historyMeta(entry));
        item->setData(0, HistStatus, status);
        item->setData(0, HistStatusKind, kind);
        item->setData(0, HistHay, (method + ' ' + url + ' ' + status + ' ' + entry.reason).toLower());
        item->setToolTip(0, QString("%1\n%2  %3 ms")
                                .arg(url, entry.time.toString("yyyy-MM-dd HH:mm:ss"))
                                .arg(entry.ms));
        m_historyTree->addTopLevelItem(item);
    }
    if (m_dataCount)
        m_dataCount->setText(m_entries.size() == 1 ? tr("1 request") : tr("%1 requests").arg(m_entries.size()));
    applyHistoryFilter();
    if (m_historyTree->topLevelItemCount() > 0)
        m_historyTree->setCurrentItem(m_historyTree->topLevelItem(0));
}

void MainWindow::recolorHistory()
{
    // Row colours are read from the theme while painting
    m_historyTree->viewport()->update();
}

void MainWindow::applyHistoryFilter()
{
    const QString needle = m_filter->text().trimmed().toLower();
    const int chip = m_chips->checkedId();
    QDate lastDay;
    int shown = 0;
    for (int i = 0; i < m_historyTree->topLevelItemCount(); ++i) {
        auto *item = m_historyTree->topLevelItem(i);
        const int index = item->data(0, Qt::UserRole).toInt();
        bool visible = index >= 0 && index < m_entries.size();
        if (visible) {
            const HistoryEntry &entry = m_entries.at(index);
            if (!needle.isEmpty() && !item->data(0, HistHay).toString().contains(needle))
                visible = false;
            else if (chip == 1 && !(entry.status >= 400 || (entry.status == 0 && !entry.reason.isEmpty())))
                visible = false;
            else if ((chip == 2 || chip == 3)
                     && entry.request.value("method").toString() != (chip == 2 ? QStringLiteral("GET") : QStringLiteral("POST")))
                visible = false;
            else if (chip == 4 && entry.time.date() != QDate::currentDate())
                visible = false;
        }
        item->setHidden(!visible);

        // The first visible row of each day carries the day heading
        QString group;
        if (visible) {
            ++shown;
            const QDate day = m_entries.at(index).time.date();
            if (day != lastDay) {
                lastDay = day;
                group = historyDay(day);
            }
        }
        if (item->data(0, HistGroup).toString() != group)
            item->setData(0, HistGroup, group);
    }
    m_historyTree->doItemsLayout();
    m_historyTitle->setText(shown == m_entries.size() ? tr("History  %1").arg(m_entries.size())
                                                      : tr("History  %1 of %2").arg(shown).arg(m_entries.size()));
}

void MainWindow::toggleSidebar()
{
    m_sideContainer->setVisible(!m_sideContainer->isVisible());
}

void MainWindow::focusFilter()
{
    m_sideContainer->show();
    m_panels->setCurrentIndex(0);
    m_historyButton->setChecked(true);
    m_filter->setFocus();
    m_filter->selectAll();
}
