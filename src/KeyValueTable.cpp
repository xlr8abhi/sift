#include "AppFont.h"
#include "KeyValueTable.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>

namespace {

constexpr int kMaxLines = 6;  // the cell being edited grows to this many lines, then scrolls
constexpr int kMinRow = 34;
constexpr int kSidePad = 18;  // cell padding plus the editor's border and padding

// Editor of a Key / Value cell: wraps, grows with its text, never holds a line break
class CellEdit : public QPlainTextEdit
{
public:
    using QPlainTextEdit::QPlainTextEdit;

protected:
    void insertFromMimeData(const QMimeData *source) override
    {
        if (!source->hasText())
            return;
        QString text = source->text();
        text.replace(QRegularExpression("[\\r\\n]+"), " ");
        insertPlainText(text);
    }
};

// The last row is the "type here" row: draw its cells like empty input fields.
class RowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!multiline(index))
            return QStyledItemDelegate::createEditor(parent, option, index);
        auto *edit = new CellEdit(parent);
        edit->setObjectName("cellEdit");
        edit->setFont(option.font);
        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        edit->setTabChangesFocus(true);
        m_editor = edit;
        m_editIndex = index;
        auto *table = qobject_cast<QTableWidget *>(this->parent());
        QObject::connect(edit, &QPlainTextEdit::textChanged, edit, [this, table] {
            if (table && m_editIndex.isValid())
                table->resizeRowToContents(m_editIndex.row());
        });
        return edit;
    }

    void destroyEditor(QWidget *editor, const QModelIndex &index) const override
    {
        const bool mine = editor == m_editor;
        if (mine)
            m_editIndex = QPersistentModelIndex();
        QStyledItemDelegate::destroyEditor(editor, index);
        if (mine) {
            // the row shrinks back to one line
            if (auto *table = qobject_cast<QTableWidget *>(this->parent()))
                QTimer::singleShot(0, table, [table] { table->resizeRowsToContents(); });
        }
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *edit = qobject_cast<QPlainTextEdit *>(editor)) {
            edit->setPlainText(index.data(Qt::EditRole).toString());
            edit->moveCursor(QTextCursor::End);
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        if (auto *edit = qobject_cast<QPlainTextEdit *>(editor)) {
            model->setData(index, edit->toPlainText(), Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(kMinRow);
        const auto *table = qobject_cast<const QTableWidget *>(parent());
        // at rest a cell is one line cut with "..."; only the cell being edited grows
        if (!table || !multiline(index) || !m_editor || m_editIndex != index)
            return size;
        const QString text = m_editor->toPlainText();
        const QFontMetrics fm(option.font);
        const int width = qMax(40, table->columnWidth(index.column()) - kSidePad);
        const QRect bounds = fm.boundingRect(QRect(0, 0, width, 100000), Qt::TextWordWrap | Qt::TextWrapAnywhere, text);
        const int lines = qBound(1, (bounds.height() + fm.lineSpacing() - 1) / fm.lineSpacing(), kMaxLines);
        size.setHeight(qMax(kMinRow, lines * fm.lineSpacing() + 14));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto *table = qobject_cast<const QTableWidget *>(parent());
        const bool lastRow = table && index.row() == table->rowCount() - 1;
        const auto *kv = qobject_cast<const KeyValueTable *>(parent());
        const bool hovered = kv && !lastRow && index.row() == kv->hoverRow();
        if (hovered) {
            QColor wash = option.palette.color(QPalette::Text);
            wash.setAlpha(14);
            painter->fillRect(option.rect, wash);
        }
        if (hovered && index.column() == 2) {
            QStyledItemDelegate::paint(painter, option, index);
            drawTrash(painter, kv->trashRect(index.row()), option.palette, kv->overTrash());
            return;
        }
        if (!lastRow || index.column() == 0 || !index.data().toString().isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const QColor text = option.palette.color(QPalette::Text);
        const QColor base = option.palette.color(QPalette::Base);
        auto mix = [](const QColor &a, const QColor &b, qreal t) {
            return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                                    a.blueF() * (1 - t) + b.blueF() * t);
        };
        const QRect box = option.rect.adjusted(index.column() == 1 ? 6 : 2, 4, index.column() == 1 ? -2 : -6, -4);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(mix(base, text, 0.22), 1, Qt::DashLine));
        painter->setBrush(mix(base, text, 0.04));
        painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        painter->setFont(option.font);
        painter->drawText(box.adjusted(10, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          index.column() == 1 ? QObject::tr("Add key") : QObject::tr("Add value"));
        painter->restore();
    }

    static void drawTrash(QPainter *p, const QRect &box, const QPalette &pal, bool lit)
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (lit) {
            QColor wash = pal.color(QPalette::Text);
            wash.setAlpha(34);
            p->setPen(Qt::NoPen);
            p->setBrush(wash);
            p->drawRoundedRect(QRectF(box).adjusted(1, 1, -1, -1), 6, 6);
        }
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(pal.color(lit ? QPalette::Text : QPalette::PlaceholderText), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal cx = box.center().x() + 0.5;
        const qreal cy = box.center().y() + 0.5;
        p->drawLine(QPointF(cx - 5.5, cy - 4), QPointF(cx + 5.5, cy - 4));
        p->drawLine(QPointF(cx - 2, cy - 4), QPointF(cx - 2, cy - 6));
        p->drawLine(QPointF(cx - 2, cy - 6), QPointF(cx + 2, cy - 6));
        p->drawLine(QPointF(cx + 2, cy - 6), QPointF(cx + 2, cy - 4));
        QPainterPath body;
        body.moveTo(cx - 4.5, cy - 2);
        body.lineTo(cx - 3.8, cy + 5.5);
        body.lineTo(cx + 3.8, cy + 5.5);
        body.lineTo(cx + 4.5, cy - 2);
        p->drawPath(body);
        p->drawLine(QPointF(cx - 1.5, cy), QPointF(cx - 1.5, cy + 3.5));
        p->drawLine(QPointF(cx + 1.5, cy), QPointF(cx + 1.5, cy + 3.5));
        p->restore();
    }

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        // leave room for the trash icon at the end of the value cell
        const auto *table = qobject_cast<const KeyValueTable *>(parent());
        if (table && index.column() == 2 && index.row() < table->rowCount() - 1) {
            const int room = qMax(20, table->columnWidth(2) - kSidePad - 30);
            option->text = QFontMetrics(option->font).elidedText(option->text, Qt::ElideRight, room);
        }
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *edit = dynamic_cast<CellEdit *>(object);
        if (edit && event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                emit commitData(edit);
                emit closeEditor(edit, QAbstractItemDelegate::NoHint);
                return true;
            }
            if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                emit commitData(edit);
                emit closeEditor(edit, key == Qt::Key_Tab ? QAbstractItemDelegate::EditNextItem
                                                          : QAbstractItemDelegate::EditPreviousItem);
                return true;
            }
            if (key == Qt::Key_Escape) {
                emit closeEditor(edit, QAbstractItemDelegate::RevertModelCache);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

private:
    // Key and Value cells wrap; the checkbox column does not
    bool multiline(const QModelIndex &index) const
    {
        return index.column() > 0;
    }

    mutable QPointer<QPlainTextEdit> m_editor;
    mutable QPersistentModelIndex m_editIndex;
};

}

KeyValueTable::KeyValueTable(QWidget *parent)
    : QTableWidget(0, 3, parent)
{
    setHorizontalHeaderLabels({QString(), tr("Key"), tr("Value")});
    verticalHeader()->hide();
    horizontalHeader()->hide(); // the "Add key / Add value" row already names the columns
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setColumnWidth(0, 32);
    setColumnWidth(1, 260);
    setFont(AppFont::mono());
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setShowGrid(false);
    setWordWrap(false); // at rest a cell is one line, cut with "..."
    setEditTriggers(QAbstractItemView::AllEditTriggers); // one click starts typing
    setItemDelegate(new RowDelegate(this));
    verticalHeader()->setDefaultSectionSize(kMinRow);
    setAlternatingRowColors(false);
    viewport()->setMouseTracking(true);

    appendRow();
    connect(this, &QTableWidget::itemChanged, this, [this] {
        if (m_busy)
            return;
        normalize();
        emit changed();
        resizeRowsToContents();
    });
    // wrapped cells change height with the column width
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this] { resizeRowsToContents(); });
}

bool KeyValueTable::rowHasText(int row) const
{
    const auto *key = item(row, 1);
    const auto *value = item(row, 2);
    return (key && !key->text().isEmpty()) || (value && !value->text().isEmpty());
}

void KeyValueTable::appendRow()
{
    const bool wasBusy = m_busy;
    m_busy = true;
    const int row = rowCount();
    insertRow(row);

    auto *check = new QTableWidgetItem;
    check->setFlags(Qt::NoItemFlags);
    setItem(row, 0, check);
    setItem(row, 1, new QTableWidgetItem);
    setItem(row, 2, new QTableWidgetItem);
    m_busy = wasBusy;
}

void KeyValueTable::normalize()
{
    m_busy = true;
    for (int row = 0; row < rowCount(); ++row) {
        auto *check = item(row, 0);
        const bool hasText = rowHasText(row);
        const bool checkable = check->flags() & Qt::ItemIsUserCheckable;
        if (hasText && !checkable) {
            check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            check->setCheckState(Qt::Checked);
        } else if (!hasText && checkable && row != rowCount() - 1) {
            removeRow(row);
            --row;
        }
    }
    if (rowCount() == 0 || rowHasText(rowCount() - 1))
        appendRow();
    m_busy = false;
}

KeyValueTable::Items KeyValueTable::items(bool enabledOnly) const
{
    Items out;
    for (int row = 0; row < rowCount(); ++row) {
        if (!rowHasText(row))
            continue;
        if (enabledOnly && item(row, 0)->checkState() != Qt::Checked)
            continue;
        const QString key = item(row, 1)->text().trimmed();
        if (key.isEmpty())
            continue;
        out.append({key, item(row, 2)->text()});
    }
    return out;
}

int KeyValueTable::activeCount() const
{
    return items(true).size();
}

void KeyValueTable::setItems(const Items &list)
{
    QList<Row> out;
    for (const auto &pair : list)
        out.append({pair.first, pair.second, true});
    setRows(out);
}

QList<KeyValueTable::Row> KeyValueTable::rows() const
{
    QList<Row> out;
    for (int row = 0; row < rowCount(); ++row) {
        if (!rowHasText(row))
            continue;
        out.append({item(row, 1)->text(), item(row, 2)->text(),
                    item(row, 0)->checkState() == Qt::Checked});
    }
    return out;
}

void KeyValueTable::setRows(const QList<Row> &list)
{
    m_busy = true;
    setRowCount(0);
    for (const auto &entry : list) {
        appendRow();
        const int row = rowCount() - 1;
        item(row, 1)->setText(entry.key);
        item(row, 2)->setText(entry.value);
    }
    appendRow();
    m_busy = false;
    normalize();
    resizeRowsToContents();

    m_busy = true;
    for (int row = 0; row < list.size() && row < rowCount(); ++row) {
        if (item(row, 0)->flags() & Qt::ItemIsUserCheckable)
            item(row, 0)->setCheckState(list.at(row).enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_busy = false;
}

void KeyValueTable::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete && state() != QAbstractItemView::EditingState) {
        const auto rows = selectionModel()->selectedRows();
        QList<int> indexes;
        for (const auto &index : rows)
            indexes.append(index.row());
        std::sort(indexes.begin(), indexes.end(), std::greater<int>());
        m_busy = true;
        for (int row : indexes)
            removeRow(row);
        m_busy = false;
        normalize();
        emit changed();
        return;
    }
    QTableWidget::keyPressEvent(event);
}

QRect KeyValueTable::trashRect(int row) const
{
    const QRect cell = visualRect(model()->index(row, 2));
    return QRect(cell.right() - 28, cell.center().y() - 11, 24, 22);
}

void KeyValueTable::setHover(int row, bool overTrash)
{
    if (row == m_hoverRow && overTrash == m_overTrash)
        return;
    const int old = m_hoverRow;
    m_hoverRow = row;
    m_overTrash = overTrash;
    viewport()->setCursor(overTrash ? Qt::PointingHandCursor : Qt::ArrowCursor);
    for (int r : {old, row}) {
        if (r >= 0 && r < rowCount())
            viewport()->update(visualRect(model()->index(r, 0)).united(visualRect(model()->index(r, 2))));
    }
}

void KeyValueTable::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    int row = rowAt(pos.y());
    if (row < 0 || row >= rowCount() - 1)
        row = -1; // the "Add" row has nothing to delete
    setHover(row, row >= 0 && trashRect(row).contains(pos));
    QTableWidget::mouseMoveEvent(event);
}

void KeyValueTable::leaveEvent(QEvent *event)
{
    setHover(-1, false);
    QTableWidget::leaveEvent(event);
}

void KeyValueTable::removeRowAt(int row)
{
    m_busy = true;
    removeRow(row);
    m_busy = false;
    normalize();
    resizeRowsToContents();
    emit changed();
}

void KeyValueTable::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const int row = rowAt(pos.y());
    if (event->button() == Qt::LeftButton && row >= 0 && row == m_hoverRow && trashRect(row).contains(pos)) {
        removeRowAt(row);
        setHover(-1, false);
        // the next row slid under the mouse
        const QPoint now = viewport()->mapFromGlobal(QCursor::pos());
        int next = rowAt(now.y());
        if (next >= 0 && next < rowCount() - 1 && viewport()->rect().contains(now))
            setHover(next, trashRect(next).contains(now));
        return;
    }
    QTableWidget::mousePressEvent(event);
}
