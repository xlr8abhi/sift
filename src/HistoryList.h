#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>

// Item data used by HistoryDelegate. Role Qt::UserRole holds the entry index.
enum HistoryRole {
    HistMethod = Qt::UserRole + 1,
    HistHost,
    HistPath,
    HistMeta,       // second line: "2m ago · 340ms"
    HistStatus,     // text of the status chip
    HistStatusKind, // 0 ok, 1 warn, 2 error, 3 not sent
    HistGroup,      // day heading drawn above this row; empty for none
    HistHay,        // lower-case text the filter searches
};

// The History list: a flat QTreeWidget whose rows are painted by HistoryDelegate.
// It tracks the hovered row for the remove button and refreshes relative times
// once a minute, but only while it is visible.
class HistoryTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit HistoryTree(QWidget *parent = nullptr);

    QModelIndex hoverIndex() const { return m_hover; }
    bool hoverOnRemove() const { return m_hoverRemove; }
    // Where the remove button sits inside a row's rectangle.
    static QRect removeButtonRect(const QRect &itemRect);

signals:
    void removeRequested(int entryIndex);
    // Enter on the current row. Mouse clicks use QTreeWidget::itemClicked instead; itemActivated
    // is not used because on single-click desktops a click emits both and the item would open twice.
    void enterPressed(QTreeWidgetItem *item);
    void tick(); // about once a minute while visible, and when shown

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    bool onRemoveButton(const QPoint &pos, QModelIndex *index) const;
    void clearHover();

    QPersistentModelIndex m_hover;
    bool m_hoverRemove = false;
    bool m_swallowRelease = false;
    QTimer m_timer;
};

class HistoryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
