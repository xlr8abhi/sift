#include "Style.h"

#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QTimer>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStandardPaths>
#include <QSet>
#include <QSharedPointer>
#include <QStyle>
#include <functional>
#include <QWidget>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

QString rgba(const QColor &c, qreal alpha)
{
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QString crossPath(const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/cross-" + color.name().mid(1) + ".png";
    if (!QFile::exists(path)) {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(6, 6, 14, 14);
        p.drawLine(14, 6, 6, 14);
        pm.save(path);
    }
    return path;
}

QString chevronPath(const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/chevron-" + color.name().mid(1) + ".png";
    if (!QFile::exists(path)) {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({QPointF(5, 8), QPointF(10, 13), QPointF(15, 8)}));
        pm.save(path);
    }
    return path;
}

}

namespace Style {

namespace {

// Qt 6 polishes a widget when it is first shown only if QWidget::ensurePolished() has not run for
// it yet, and clearing WA_WState_Polished does not reset that. So a widget we unpolished while it
// was hidden would never be styled again. This filter polishes it by hand when it is shown.
class ShowPolisher : public QObject
{
public:
    QSet<QWidget *> deferred;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show && watched->isWidgetType()) {
            auto *w = static_cast<QWidget *>(watched);
            if (deferred.remove(w)) {
                w->style()->polish(w);
                w->setAttribute(Qt::WA_WState_Polished, true);
                w->updateGeometry();
                w->update();
            }
        }
        return false;
    }
};

ShowPolisher &polisher()
{
    static ShowPolisher *p = [] {
        auto *filter = new ShowPolisher;
        qApp->installEventFilter(filter);
        return filter;
    }();
    return *p;
}

void unpolishLater(QWidget *w)
{
    w->style()->unpolish(w); // drops it from the stylesheet cache, so it is not restyled now
    w->setAttribute(Qt::WA_WState_Polished, false);
    polisher().deferred.insert(w);
}

}

void deferHiddenWidgets()
{
    // Not conditional on WA_WState_Polished: setting a stylesheet polishes every widget through
    // QStyleSheetStyle without setting that flag, so an "unpolished" hidden widget can still hold
    // the previous theme's palette. Unpolishing is cheap when there is nothing to undo.
    for (QWidget *w : QApplication::allWidgets()) {
        if (!w->isWindow() && !w->isVisible())
            unpolishLater(w);
    }
}

void deferHiddenWidgetsInBackground()
{
    auto pending = QSharedPointer<QList<QPointer<QWidget>>>::create();
    for (QWidget *w : QApplication::allWidgets()) {
        if (!w->isWindow() && !w->isVisible())
            pending->append(w);
    }
    auto step = QSharedPointer<std::function<void()>>::create();
    *step = [pending, step] {
        for (int n = 0; n < 150 && !pending->isEmpty(); ++n) {
            QPointer<QWidget> w = pending->takeLast();
            if (w && !w->isVisible())
                unpolishLater(w);
        }
        if (!pending->isEmpty())
            QTimer::singleShot(0, qApp, [step] { (*step)(); });
    };
    QTimer::singleShot(0, qApp, [step] { (*step)(); });
}

void install()
{
    const QPalette pal = QApplication::palette();
    const QColor text = pal.color(QPalette::Text);
    const QColor bg = pal.color(QPalette::Base);
    const QColor chrome = pal.color(QPalette::Window);
    const QColor accent = pal.color(QPalette::Highlight);

    const QString tx = text.name();
    const QString base = bg.name();
    const QString win = chrome.name();
    const QString dm = pal.color(QPalette::PlaceholderText).name();
    const QString ln = mix(bg, text, 0.16).name();
    const QString ln2 = mix(bg, text, 0.28).name();
    const QString field = mix(bg, text, 0.045).name();
    const QString p2 = mix(bg, text, 0.09).name();
    const QString ac = accent.name();
    const QString onAc = pal.color(QPalette::HighlightedText).name();
    const QString wash = rgba(accent, 0.16);
    const QString chev = chevronPath(pal.color(QPalette::PlaceholderText));

    QString css = R"(
QMainWindow { background: %WIN%; }
QToolTip { background: %FIELD%; color: %TX%; border: 1px solid %LN%; padding: 4px 6px; }
QMenu { background: %WIN%; border: 1px solid %LN%; }
QStatusBar { background: %WIN%; color: %DM%; border-top: 1px solid %LN%; }
QStatusBar::item { border: none; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: %LN2%; border-radius: 3px; min-height: 28px; margin: 2px 3px; }
QScrollBar::handle:vertical:hover { background: %DM%; margin: 2px 2px; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: %LN2%; border-radius: 3px; min-width: 28px; margin: 3px 2px; }
QScrollBar::handle:horizontal:hover { background: %DM%; margin: 2px 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QLineEdit { background: %FIELD%; border: 1px solid %LN%; border-radius: 6px; padding: 4px 8px;
            selection-background-color: %AC%; selection-color: %ONAC%; }
QLineEdit:focus { border-color: %AC%; }
QPlainTextEdit { background: %BASE%; border: none; selection-background-color: %AC%; selection-color: %ONAC%; }
QTextBrowser#render { background: #ffffff; color: #000000; border: none; }

QPushButton { background: %P2%; border: 1px solid %LN%; border-radius: 6px; padding: 5px 12px; color: %TX%; }
QPushButton:hover { border-color: %DM%; }
QPushButton:pressed { background: %LN%; }
QToolButton { border: none; border-radius: 4px; padding: 2px 6px; color: %TX%; }
QToolButton:hover { background: %P2%; }

QComboBox { background: %FIELD%; border: 1px solid %LN%; border-radius: 6px; padding: 4px 8px; }
QComboBox:hover { border-color: %DM%; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox::down-arrow { image: url(%CHEV%); width: 10px; height: 10px; }
QComboBox QAbstractItemView { background: %WIN%; border: 1px solid %LN%; outline: 0; padding: 3px;
                              selection-background-color: %WASH%; selection-color: %TX%; }
QComboBox QAbstractItemView::item { padding: 5px 10px; min-height: 20px; border-radius: 4px; }

QTabWidget::pane { border: none; border-top: 1px solid %LN%; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; border: none; }
#reqTabs > QTabBar::tab, #respTabs > QTabBar::tab { color: %DM%; }
#reqTabs > QTabBar::tab:hover:!selected, #respTabs > QTabBar::tab:hover:!selected { color: %TX%; }

#docTabs > QTabBar { background: %WIN%; }
#docTabs > QTabBar::tab { padding: 0 8px 0 14px; height: 34px; min-width: 110px; max-width: 220px;
                          border-right: 1px solid %LN%; border-top: 2px solid transparent; }
#docTabs > QTabBar::tab:selected { background: %BASE%; border-top: 2px solid %AC%; }
#docTabs > QTabBar::scroller { width: 0px; }
#docTabs > QTabBar::tab:hover:!selected { background: %P2%; }
#docTabs > QTabBar::close-button { image: url(%XICON%); subcontrol-position: right; margin-right: 2px;
                                   border-radius: 4px; padding: 2px; }
#docTabs > QTabBar::close-button:hover { background: %LN%; }

#reqTabs > QTabBar::tab, #respTabs > QTabBar::tab { padding: 7px 8px 6px; margin-right: 0; border-bottom: 2px solid transparent; }
#reqTabs > QTabBar::tab:selected, #respTabs > QTabBar::tab:selected { color: %TX%; border-bottom: 2px solid %AC%; }
#reqTabs::tab-bar, #respTabs::tab-bar { left: 8px; }

QTableView { background: %BASE%; border: none; gridline-color: transparent; outline: 0;
             selection-background-color: %WASH%; selection-color: %TX%; }
QTableView::item { border-bottom: 1px solid %LN%; padding: 2px 6px; }
QPlainTextEdit#cellEdit { background: %BASE%; border: 1px solid %AC%; border-radius: 5px; padding: 2px 5px; }
QHeaderView { background: %BASE%; }
QHeaderView::section { background: %BASE%; color: %DM%; border: none; border-bottom: 1px solid %LN%;
                       padding: 4px 8px; }
QTableCornerButton::section { background: %BASE%; border: none; }

QTreeView { background: %BASE%; border: none; outline: 0; }
QTreeView::item:selected { background: %WASH%; color: %TX%; }
QTreeView::item:hover:!selected { background: %P2%; }

QToolButton#chip { color: %DM%; border: 1px solid %LN%; border-radius: 9px; padding: 1px 9px; }
QToolButton#chip:hover:!checked { color: %TX%; background: transparent; border-color: %DM%; }
QToolButton#chip:checked { color: %AC%; border-color: %AC%; background: %WASH%; }

QFrame#stepper { background: %BASE%; border: 1px solid %LN%; border-radius: 6px; }
QFrame#stepper QToolButton { padding: 2px 11px; border-radius: 4px; color: %DM%; }
QFrame#stepper QToolButton:hover { color: %TX%; background: %P2%; }
QFrame#stepper QLabel { padding: 0 4px; }
QToolButton#linkButton { color: %AC%; padding: 2px 0; }
QToolButton#linkButton:hover { color: %ACH%; }
QToolButton#quietLink { color: %DM%; padding: 2px 0; }
QToolButton#quietLink:hover { color: %TX%; }
QScrollArea#settingsScroll { background: transparent; border: none; }
QWidget#settingsBody { background: transparent; }

QToolButton#newTab { padding: 0 14px; min-height: 34px; border-radius: 0; color: %DM%; font-size: 16px; }
QToolButton#newTab:hover { color: %TX%; background: %P2%; }
QToolButton#tabList { padding: 0 26px 0 12px; min-height: 34px; border-radius: 0; border-left: 1px solid %LN%; color: %TX%; }
QToolButton#tabList:hover, QToolButton#tabList:open { background: %P2%; }
QToolButton#tabList::menu-indicator { image: url(%CHEV%); width: 10px; height: 10px;
                                      subcontrol-origin: padding; subcontrol-position: right center; right: 9px; }

QSplitter::handle { background: transparent; }

#urlBar { background: %FIELD%; border: 1px solid %LN%; border-radius: 8px; }
#urlBar QLineEdit { background: transparent; border: none; padding: 0 12px; }
#urlBar QComboBox { background: transparent; border: none; border-right: 1px solid %LN%; border-radius: 0;
                    padding: 0 8px 0 12px; min-width: 56px; }
#urlBar QPushButton#sendButton { background: %AC%; color: %ONAC%; border: none; border-radius: 0;
                                 border-top-right-radius: 7px; border-bottom-right-radius: 7px;
                                 padding: 0 16px; }
#urlBar QPushButton#sendButton:hover { background: %ACH%; }

QLabel#panelTitle { color: %DM%; padding: 12px 12px 8px 12px; }
QLabel#meta { color: %DM%; }
QLabel#fieldLabel { color: %TX%; }
QLabel#sectionLabel { color: %DM%; padding: 6px 12px 8px 12px; border-top: 1px solid %LN%; margin-top: 2px; }
)";

    const QMap<QString, QString> tokens = {
        {"%WIN%", win}, {"%BASE%", base}, {"%TX%", tx}, {"%DM%", dm}, {"%LN%", ln}, {"%LN2%", ln2},
        {"%FIELD%", field}, {"%P2%", p2}, {"%AC%", ac}, {"%ONAC%", onAc}, {"%WASH%", wash},
        {"%ACH%", accent.lighter(112).name()}, {"%CHEV%", chev}, {"%XICON%", crossPath(pal.color(QPalette::PlaceholderText))},
    };
    for (auto it = tokens.begin(); it != tokens.end(); ++it)
        css.replace(it.key(), it.value());
    // Replacing a live stylesheet re-resolves the old one for every widget (~240 ms);
    // clearing first and then setting the new one costs about a quarter of that.
    qApp->setStyleSheet(QString());
    qApp->setStyleSheet(css);
}

}
