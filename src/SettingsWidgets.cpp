#include "SettingsWidgets.h"

#include "AppFont.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

}

Switch::Switch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize Switch::sizeHint() const
{
    return QSize(34, 20);
}

void Switch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    const QColor accent = ThemeManager::instance().colors().accent;
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? accent : withAlpha(palette().color(QPalette::Text), 70));
    p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const qreal d = track.height() - 6;
    const qreal x = isChecked() ? track.right() - 3 - d : track.left() + 3;
    p.setBrush(palette().color(QPalette::Base));
    p.drawEllipse(QRectF(x, track.top() + 3, d, d));
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent, 1.5));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), height() / 2.0, height() / 2.0);
    }
}

Segmented::Segmented(const QStringList &labels, QWidget *parent)
    : QWidget(parent)
    , m_labels(labels)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Segmented::setCurrent(int index)
{
    if (index == m_current)
        return;
    m_current = index;
    update();
}

QSize Segmented::sizeHint() const
{
    return QSize(200, QFontMetrics(font()).height() + 12);
}

void Segmented::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor accent = ThemeManager::instance().colors().accent;
    const QColor text = palette().color(QPalette::Text);
    const QColor muted = palette().color(QPalette::PlaceholderText);
    const int n = m_labels.size();
    if (n == 0)
        return;
    const qreal w = box.width() / n;

    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(box, 6, 6);

    if (m_current >= 0 && m_current < n) {
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(box, 6, 6);
        p.setClipPath(clip);
        p.fillRect(QRectF(box.left() + w * m_current, box.top(), w, box.height()), withAlpha(accent, 48));
        p.restore();
    }

    p.setPen(withAlpha(text, 36));
    for (int i = 1; i < n; ++i)
        p.drawLine(QPointF(box.left() + w * i, box.top() + 1), QPointF(box.left() + w * i, box.bottom() - 1));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(hasFocus() ? accent : withAlpha(text, 50), hasFocus() ? 1.5 : 1.0));
    p.drawRoundedRect(box, 6, 6);

    for (int i = 0; i < n; ++i) {
        p.setPen(i == m_current ? accent : muted);
        p.drawText(QRectF(box.left() + w * i, box.top(), w, box.height()), Qt::AlignCenter, m_labels.at(i));
    }
}

void Segmented::mousePressEvent(QMouseEvent *event)
{
    const int n = m_labels.size();
    if (n == 0 || event->button() != Qt::LeftButton)
        return;
    const int index = qBound(0, int(event->position().x() / (double(width()) / n)), n - 1);
    setCurrent(index);
    emit activated(index);
}

void Segmented::keyPressEvent(QKeyEvent *event)
{
    const int n = m_labels.size();
    int next = m_current;
    if (event->key() == Qt::Key_Left)
        next = m_current <= 0 ? 0 : m_current - 1;
    else if (event->key() == Qt::Key_Right)
        next = qMin(n - 1, m_current + 1);
    else {
        QWidget::keyPressEvent(event);
        return;
    }
    if (next != m_current) {
        setCurrent(next);
        emit activated(next);
    }
}

ThemeRow::ThemeRow(QWidget *parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize ThemeRow::sizeHint() const
{
    return QSize(200, QFontMetrics(font()).height() + 16);
}

void ThemeRow::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    m_hover = true;
    update();
}

void ThemeRow::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    m_hover = false;
    update();
}

void ThemeRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const SemanticColors c = ThemeManager::instance().colors();
    const QColor text = palette().color(QPalette::Text);
    const QColor muted = palette().color(QPalette::PlaceholderText);

    p.setBrush(palette().color(QPalette::Base));
    p.setPen(QPen(hasFocus() ? c.accent : withAlpha(text, m_hover ? 110 : 50), hasFocus() ? 1.5 : 1.0));
    p.drawRoundedRect(box, 6, 6);

    qreal x = box.left() + 12;
    p.setPen(Qt::NoPen);
    for (const QColor &dot : {c.accent, c.ok, c.warn, c.err}) {
        p.setBrush(dot);
        p.drawEllipse(QPointF(x + 4, box.center().y()), 4, 4);
        x += 11;
    }
    x += 6;

    const Theme &theme = ThemeManager::instance().current();
    const QString name = theme.system ? tr("KDE system theme") : theme.name;
    p.setPen(text);
    const QRectF label(x, box.top(), box.right() - x - 24, box.height());
    p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font()).elidedText(name, Qt::ElideRight, int(label.width())));

    p.setPen(QPen(muted, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF c0(box.right() - 14, box.center().y());
    p.drawPolyline(QPolygonF({c0 + QPointF(-2, -4), c0 + QPointF(2, 0), c0 + QPointF(-2, 4)}));
}

FontPreview::FontPreview(QWidget *parent)
    : QWidget(parent)
{
    setFont(AppFont::mono()); // AppFont keeps widgets with the mono family up to date
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize FontPreview::sizeHint() const
{
    return QSize(200, QFontMetrics(font()).height() * 2 + 20);
}

void FontPreview::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        updateGeometry();
        update();
    }
}

void FontPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const SemanticColors c = ThemeManager::instance().colors();
    const QColor text = palette().color(QPalette::Text);

    p.setBrush(palette().color(QPalette::Base));
    p.setPen(withAlpha(text, 50));
    p.drawRoundedRect(box, 6, 6);

    const QFontMetrics fm(font());
    p.setFont(font());
    struct Part { QString s; QColor color; };
    const QList<QList<Part>> lines = {
        {{"\"status\"", c.key}, {": ", text}, {"\"ok\"", c.string}, {",", text}},
        {{"\"count\"", c.key}, {": ", text}, {"42", c.number}},
    };
    qreal y = box.top() + 10;
    for (const auto &line : lines) {
        qreal x = box.left() + 10;
        for (const Part &part : line) {
            p.setPen(part.color);
            p.drawText(QPointF(x, y + fm.ascent()), part.s);
            x += fm.horizontalAdvance(part.s);
        }
        y += fm.height();
    }
}
