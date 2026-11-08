#include "DocTabBar.h"

#include "Theme.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionTab>

namespace {

constexpr int kPadLeft = 14;
constexpr int kPadRight = 8;
constexpr int kGap = 7;
constexpr int kClose = 18;
constexpr int kMinTab = 110;
constexpr int kMaxTab = 220;

QFont badgeFont(QFont font)
{
    if (font.pointSizeF() > 0)
        font.setPointSizeF(qMax(6.5, font.pointSizeF() * 0.78));
    font.setBold(true);
    return font;
}

QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

// Width of "POST" / "DELETE" etc. is not fixed; every tab reserves the same badge width
// so the titles line up across tabs.
int badgeWidth(const QFont &font)
{
    return QFontMetrics(badgeFont(font)).horizontalAdvance(QStringLiteral("DELETE"));
}

// Paints badge + title into rect (rect.right() is where the title must stop).
void paintLabel(QPainter *p, const QRect &rect, const QFont &font, const QVariantMap &data, const QPalette &pal,
                bool bright)
{
    const QString method = data.value("method").toString();
    const bool fresh = data.value("fresh").toBool();
    const QFontMetrics fm(font);
    const int bw = badgeWidth(font);

    p->setFont(badgeFont(font));
    QColor badge = methodColor(method);
    if (fresh || !bright)
        badge.setAlpha(fresh ? 120 : 190);
    p->setPen(badge);
    p->drawText(QRect(rect.left(), rect.top(), bw, rect.height()), Qt::AlignLeft | Qt::AlignVCenter, method);

    p->setFont(font);
    const int x = rect.left() + bw + kGap;
    const int w = qMax(0, rect.right() - x + 1);
    const QString host = data.value("host").toString();
    const QString path = data.value("path").toString();
    const QColor text = pal.color(QPalette::Text);
    const QColor muted = pal.color(QPalette::PlaceholderText);
    const QString hostShown = fm.elidedText(host, Qt::ElideRight, w);
    p->setPen(fresh ? muted : (bright ? text : withAlpha(text, 175)));
    p->drawText(QRect(x, rect.top(), w, rect.height()), Qt::AlignLeft | Qt::AlignVCenter, hostShown);
    const int hw = fm.horizontalAdvance(hostShown);
    if (hostShown == host && !path.isEmpty() && w > hw) {
        p->setPen(muted);
        p->drawText(QRect(x + hw, rect.top(), w - hw, rect.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(path, Qt::ElideRight, w - hw));
    }
}

}

namespace DocTab {

QVariantMap data(const QString &method, const QString &host, const QString &path, bool fresh)
{
    return {{"method", method}, {"host", host}, {"path", path}, {"fresh", fresh}};
}

}

DocTabBar::DocTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setMouseTracking(true);
}

QSize DocTabBar::tabSizeHint(int index) const
{
    QSize size = QTabBar::tabSizeHint(index);
    const QVariantMap d = tabData(index).toMap();
    if (d.isEmpty())
        return size;
    const QFontMetrics fm(font());
    const int text = fm.horizontalAdvance(d.value("host").toString() + d.value("path").toString());
    const int wanted = kPadLeft + badgeWidth(font()) + kGap + text + kGap + kClose + kPadRight;
    // Same limits as the old stylesheet rule: long URLs are cut with an ellipsis
    size.setWidth(qBound(kMinTab, wanted, kMaxTab));
    return size;
}

QRect DocTabBar::closeRect(int index) const
{
    const QRect r = tabRect(index);
    return QRect(r.right() - kPadRight - kClose + 1, r.center().y() - kClose / 2 + 1, kClose, kClose);
}

bool DocTabBar::closeVisible(int index) const
{
    return index == currentIndex() || index == m_hover;
}

void DocTabBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    for (int i = 0; i < count(); ++i) {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);
        opt.text.clear(); // the label is painted below
        opt.features &= ~QStyleOptionTab::HasFrame;
        opt.features |= QStyleOptionTab::HasFrame;
        style()->drawControl(QStyle::CE_TabBarTabShape, &opt, &p, this);

        const QRect rect = tabRect(i);
        const bool selected = i == currentIndex();
        const bool bright = selected || i == m_hover;
        const QRect label(rect.left() + kPadLeft, rect.top(), rect.width() - kPadLeft - kPadRight - kClose - kGap, rect.height());
        p.save();
        p.setClipRect(rect);
        paintLabel(&p, label, font(), tabData(i).toMap(), palette(), bright);

        if (closeVisible(i)) {
            const QRect box = closeRect(i);
            const bool on = i == m_hover && m_hoverClose;
            p.setRenderHint(QPainter::Antialiasing, true);
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(withAlpha(palette().color(QPalette::Text), 36));
                p.drawRoundedRect(box, 4, 4);
            }
            QPen pen(on ? palette().color(QPalette::Text) : palette().color(QPalette::PlaceholderText), 1.4);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            const QPointF c = box.center() + QPointF(0.5, 0.5);
            p.drawLine(QPointF(c.x() - 3.5, c.y() - 3.5), QPointF(c.x() + 3.5, c.y() + 3.5));
            p.drawLine(QPointF(c.x() - 3.5, c.y() + 3.5), QPointF(c.x() + 3.5, c.y() - 3.5));
        }
        p.restore();
    }
}

void DocTabBar::mouseMoveEvent(QMouseEvent *event)
{
    QTabBar::mouseMoveEvent(event);
    const QPoint pos = event->position().toPoint();
    const int index = tabAt(pos);
    const bool onClose = index >= 0 && closeVisible(index) && closeRect(index).contains(pos);
    if (index != m_hover || onClose != m_hoverClose) {
        m_hover = index;
        m_hoverClose = onClose;
        update();
    }
}

void DocTabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        const int index = tabAt(pos);
        if (index >= 0 && closeVisible(index) && closeRect(index).contains(pos)) {
            emit tabCloseRequested(index);
            return;
        }
    }
    QTabBar::mousePressEvent(event);
}

void DocTabBar::leaveEvent(QEvent *event)
{
    QTabBar::leaveEvent(event);
    m_hover = -1;
    m_hoverClose = false;
    update();
}

TabListRow::TabListRow(const QVariantMap &data, bool current, QWidget *parent)
    : QWidget(parent)
    , m_data(data)
    , m_current(current)
{
    setCursor(Qt::PointingHandCursor);
    // A fixed height: inside the scroll area the layout would otherwise squeeze
    // the rows together instead of letting the list scroll
    setFixedHeight(sizeHint().height());
}

QSize TabListRow::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(qMax(280, fm.horizontalAdvance(QLatin1Char('x')) * 34), fm.height() + 12);
}

void TabListRow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hover = true;
    update();
}

void TabListRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = false;
    update();
}

void TabListRow::mousePressEvent(QMouseEvent *event)
{
    // A release alone must not pick a row: the list opens under the mouse button that
    // opened it, and letting go of that button would otherwise select whatever is below.
    m_pressed = event->button() == Qt::LeftButton;
}

void TabListRow::mouseReleaseEvent(QMouseEvent *event)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    if (wasPressed && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
}

void TabListRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_hover || m_current) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(palette().color(QPalette::Text), m_hover ? 30 : 18));
        p.drawRoundedRect(rect().adjusted(2, 1, -2, -1), 6, 6);
    }
    paintLabel(&p, rect().adjusted(10, 0, -10, 0), font(), m_data, palette(), m_current || m_hover);
}
