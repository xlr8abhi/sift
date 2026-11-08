#include "AppFont.h"
#include "ThemePicker.h"
#include <QTimer>

#include <QApplication>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

class SwatchDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {0, 34};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.state &= ~QStyle::State_HasFocus;
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const auto &themes = ThemeManager::instance().all();
        const int i = index.data(Qt::UserRole).toInt();
        if (i < 0 || i >= themes.size())
            return;
        const Theme &theme = themes.at(i);
        const bool selected = option.state & QStyle::State_Selected;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(option.palette.color(QPalette::Mid));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

        QFont font = AppFont::mono();
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));

        const QRect r = option.rect.adjusted(10, 0, -10, 0);
        QString name = theme.name;
        if (!theme.dark)
            name += "  (light)";
        painter->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, name);

        const QStringList keys = {"black", "one_bg3", "grey", "light_grey", "red",
                                  "orange", "yellow", "green", "blue", "purple"};
        const int size = 11;
        const int gap = 7;
        int x = r.right() - keys.size() * size - (keys.size() - 1) * gap;
        const int y = r.center().y() - size / 2;
        painter->setPen(QPen(option.palette.color(QPalette::Mid), 1));
        for (const QString &key : keys) {
            painter->setBrush(theme.color(key, QColor(Qt::transparent)));
            painter->drawRoundedRect(QRect(x, y, size, size), 3, 3);
            x += size + gap;
        }
        painter->restore();
    }
};

}

ThemePicker::ThemePicker(QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    , m_original(ThemeManager::instance().current())
{
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(480, 330);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search themes…"));
    m_search->setFrame(false);
    m_search->setFont(AppFont::mono());
    m_search->installEventFilter(this);
    m_count = new QLabel;

    m_list = new QListWidget;
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new SwatchDelegate(m_list));
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *hint = new QLabel(tr("↑↓ preview    Enter apply    Esc cancel    Tab dark / light"));
    hint->setEnabled(false);

    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(12, 8, 12, 8);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_count);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(searchRow);
    layout->addWidget(m_list, 1);
    auto *hintRow = new QHBoxLayout;
    hintRow->setContentsMargins(12, 6, 12, 6);
    hintRow->addWidget(hint);
    layout->addLayout(hintRow);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(90);
    connect(m_previewTimer, &QTimer::timeout, this, [this] {
        if (m_pending >= 0)
            ThemeManager::instance().apply(ThemeManager::instance().all().at(m_pending));
    });
    connect(m_search, &QLineEdit::textChanged, this, &ThemePicker::refill);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        const int i = m_list->item(row)->data(Qt::UserRole).toInt();
        // Applying a theme restyles every widget, so wait for a pause in typing / arrow keys.
        m_pending = i;
        m_previewTimer->start();
        m_count->setText(QString("%1/%2").arg(row + 1).arg(m_list->count()));
    });
    connect(m_list, &QListWidget::itemClicked, this, [this] { accept(); });

    refill();
    m_search->setFocus();

    if (parent)
        move(parent->mapToGlobal(parent->rect().center()) - rect().center());
}

void ThemePicker::refill()
{
    const auto &themes = ThemeManager::instance().all();
    const QString needle = m_search->text().trimmed().toLower();

    const bool blocked = m_list->blockSignals(true);
    m_list->clear();
    int currentRow = -1;
    for (int i = 0; i < themes.size(); ++i) {
        const Theme &theme = themes.at(i);
        if (m_filter == 1 && !theme.dark)
            continue;
        if (m_filter == 2 && theme.dark)
            continue;
        if (!needle.isEmpty() && !theme.name.toLower().contains(needle))
            continue;
        auto *item = new QListWidgetItem;
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
        if (theme.name == m_original.name)
            currentRow = m_list->count() - 1;
    }
    m_list->blockSignals(blocked);

    const QString label = m_filter == 1 ? tr("dark") : m_filter == 2 ? tr("light") : QString();
    if (m_list->count() == 0) {
        m_count->setText(label.isEmpty() ? tr("0/0") : label + " · 0/0");
        return;
    }
    m_list->setCurrentRow(needle.isEmpty() && currentRow >= 0 ? currentRow : 0);
    m_count->setText((label.isEmpty() ? QString() : label + " · ")
                     + QString("%1/%2").arg(m_list->currentRow() + 1).arg(m_list->count()));
}

void ThemePicker::step(int delta)
{
    const int n = m_list->count();
    if (n == 0)
        return;
    m_list->setCurrentRow((m_list->currentRow() + delta + n) % n);
}

bool ThemePicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        switch (key) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept();
            return true;
        case Qt::Key_Down:
            step(1);
            return true;
        case Qt::Key_Up:
            step(-1);
            return true;
        case Qt::Key_PageDown:
            step(6);
            return true;
        case Qt::Key_PageUp:
            step(-6);
            return true;
        case Qt::Key_Tab:
            m_filter = (m_filter + 1) % 3;
            refill();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ThemePicker::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ThemePicker::accept()
{
    m_previewTimer->stop();
    const auto *item = m_list->currentItem();
    if (item) {
        const int i = item->data(Qt::UserRole).toInt();
        ThemeManager::instance().commit(ThemeManager::instance().all().at(i));
    } else {
        ThemeManager::instance().apply(m_original);
    }
    QDialog::accept();
}

void ThemePicker::reject()
{
    m_previewTimer->stop();
    ThemeManager::instance().apply(m_original);
    QDialog::reject();
}
