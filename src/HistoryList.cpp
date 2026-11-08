#include "HistoryList.h"

#include "Theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int kPadX = 10;
constexpr int kPadY = 6;
constexpr int kGap = 2;
constexpr int kHeader = 22;

QFont smallFont(QFont font)
{
    if (font.pointSizeF() > 0)
        font.setPointSizeF(qMax(7.0, font.pointSizeF() * 0.85));
    return font;
}

}

HistoryTree::HistoryTree(QWidget *parent)
    : QTreeWidget(parent)
{
    viewport()->setMouseTracking(true);
    m_timer.setInterval(60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &HistoryTree::tick);
}

QRect HistoryTree::removeButtonRect(const QRect &itemRect)
{
    return QRect(itemRect.right() - 24, itemRect.bottom() - 23, 18, 18);
}

bool HistoryTree::onRemoveButton(const QPoint &pos, QModelIndex *index) const
{
    const QModelIndex idx = indexAt(pos);
    if (index)
        *index = idx;
    return idx.isValid() && removeButtonRect(visualRect(idx)).contains(pos);
}

void HistoryTree::clearHover()
{
    if (m_hover.isValid() || m_hoverRemove) {
        m_hover = QModelIndex();
        m_hoverRemove = false;
        viewport()->unsetCursor();
        viewport()->update();
    }
}

void HistoryTree::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentItem() && !currentItem()->isHidden()) {
        emit enterPressed(currentItem());
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

void HistoryTree::mouseMoveEvent(QMouseEvent *event)
{
    QTreeWidget::mouseMoveEvent(event);
    QModelIndex idx;
    const bool remove = onRemoveButton(event->position().toPoint(), &idx);
    if (idx != QModelIndex(m_hover) || remove != m_hoverRemove) {
        m_hover = idx;
        m_hoverRemove = remove;
        viewport()->setCursor(remove ? Qt::PointingHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
}

void HistoryTree::mousePressEvent(QMouseEvent *event)
{
    QModelIndex idx;
    if (event->button() == Qt::LeftButton && onRemoveButton(event->position().toPoint(), &idx)) {
        m_swallowRelease = true;
        emit removeRequested(idx.data(Qt::UserRole).toInt());
        return;
    }
    QTreeWidget::mousePressEvent(event);
}

void HistoryTree::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_swallowRelease) {
        m_swallowRelease = false;
        return;
    }
    QTreeWidget::mouseReleaseEvent(event);
}

void HistoryTree::leaveEvent(QEvent *event)
{
    QTreeWidget::leaveEvent(event);
    clearHover();
}

void HistoryTree::showEvent(QShowEvent *event)
{
    QTreeWidget::showEvent(event);
    m_timer.start();
    emit tick();
}

void HistoryTree::hideEvent(QHideEvent *event)
{
    QTreeWidget::hideEvent(event);
    m_timer.stop();
}

QSize HistoryDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const int header = index.data(HistGroup).toString().isEmpty() ? 0 : kHeader;
    const int line1 = QFontMetrics(option.font).height();
    const int line2 = QFontMetrics(smallFont(option.font)).height();
    return QSize(0, header + kPadY + line1 + kGap + line2 + kPadY);
}

void HistoryDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.features &= ~QStyleOptionViewItem::HasDisplay;

    const QString group = index.data(HistGroup).toString();
    const QRect full = opt.rect;
    const QRect row = full.adjusted(0, group.isEmpty() ? 0 : kHeader, 0, 0);
    const QColor muted = opt.palette.color(QPalette::PlaceholderText);
    const QColor text = opt.palette.color(QPalette::Text);
    const SemanticColors colors = ThemeManager::instance().colors();

    p->save();
    p->setClipRect(full);

    if (!group.isEmpty()) {
        p->setFont(smallFont(opt.font));
        p->setPen(muted);
        p->drawText(QRect(full.left() + kPadX, full.top(), full.width() - 2 * kPadX, kHeader),
                    Qt::AlignLeft | Qt::AlignBottom, group);
    }

    // Row background (hover / selected) comes from the application stylesheet
    opt.rect = row;
    QApplication::style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, p, opt.widget);
    if (opt.state & QStyle::State_Selected)
        p->fillRect(QRect(row.left(), row.top(), 2, row.height()), colors.accent);

    const QFont font = opt.font;
    const QFont small = smallFont(font);
    const QFontMetrics fm(font);
    const QFontMetrics sfm(small);
    const int left = row.left() + kPadX;
    const int right = row.right() - kPadX + 2;
    const int y1 = row.top() + kPadY;
    const int y2 = y1 + fm.height() + kGap;

    // Method
    QFont bold = font;
    bold.setBold(true);
    const QString method = index.data(HistMethod).toString();
    const int methodW = fm.horizontalAdvance(QStringLiteral("DELETE")) + 10;
    p->setFont(bold);
    p->setPen(methodColor(method));
    p->drawText(QRect(left, y1, methodW, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, method);

    // Status chip
    const QString status = index.data(HistStatus).toString();
    const int kind = index.data(HistStatusKind).toInt();
    const QColor chipInk = kind == 0 ? colors.ok : kind == 1 ? colors.warn : kind == 2 ? colors.err : muted;
    const int chipW = fm.horizontalAdvance(status) + 12;
    const QRect chip(right - chipW, y1, chipW, fm.height());
    QColor chipFill = chipInk;
    chipFill.setAlpha(kind == 3 ? 24 : 40);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(chipFill);
    p->drawRoundedRect(chip, 5, 5);
    p->setFont(font);
    p->setPen(chipInk);
    p->drawText(chip, Qt::AlignCenter, status);

    // Host + path
    const int textX = left + methodW + 4;
    const int textW = qMax(0, chip.left() - 8 - textX);
    const QString host = index.data(HistHost).toString();
    const QString path = index.data(HistPath).toString();
    const QString hostShown = fm.elidedText(host, Qt::ElideRight, textW);
    p->setPen(text);
    p->drawText(QRect(textX, y1, textW, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, hostShown);
    const int hostW = fm.horizontalAdvance(hostShown);
    if (hostShown == host && textW > hostW && !path.isEmpty()) {
        p->setPen(muted);
        p->drawText(QRect(textX + hostW, y1, textW - hostW, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(path, Qt::ElideMiddle, textW - hostW));
    }

    // Meta line
    const auto *tree = qobject_cast<const HistoryTree *>(opt.widget);
    const bool hovered = tree && tree->hoverIndex() == index;
    const int metaW = right - textX - (hovered ? 24 : 0);
    p->setFont(small);
    p->setPen(muted);
    p->drawText(QRect(textX, y2, metaW, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText(index.data(HistMeta).toString(), Qt::ElideRight, metaW));

    // Remove button, only on the hovered row
    if (hovered) {
        const QRect box = HistoryTree::removeButtonRect(full);
        const bool on = tree->hoverOnRemove();
        if (on) {
            QColor fill = text;
            fill.setAlpha(36);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(box, 4, 4);
        }
        QPen pen(on ? text : muted, 1.4);
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        const QPointF c = box.center() + QPointF(0.5, 0.5);
        const qreal d = 3.5;
        p->drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
        p->drawLine(QPointF(c.x() - d, c.y() + d), QPointF(c.x() + d, c.y() - d));
    }
    p->restore();
}
