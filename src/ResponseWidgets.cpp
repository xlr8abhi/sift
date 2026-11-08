#include "ResponseWidgets.h"

#include "AppFont.h"
#include "FoldingEdit.h"

#include <QAbstractScrollArea>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>

IconButton::IconButton(Kind kind, QWidget *parent)
    : QAbstractButton(parent)
    , m_kind(kind)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setCheckable(kind == Kind::Wrap);
}

void IconButton::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void IconButton::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

void IconButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool lit = isChecked() || underMouse();
    QColor ink = palette().color(lit ? QPalette::Text : QPalette::PlaceholderText);
    if (lit) {
        QColor wash = palette().color(QPalette::Text);
        wash.setAlpha(isDown() ? 44 : isChecked() ? 34 : 20);
        p.setPen(Qt::NoPen);
        p.setBrush(wash);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;

    if (m_kind == Kind::Search) {
        p.drawEllipse(QPointF(cx - 1.5, cy - 1.5), 4.5, 4.5);
        p.drawLine(QPointF(cx + 1.8, cy + 1.8), QPointF(cx + 5.5, cy + 5.5));
    } else if (m_kind == Kind::Copy) {
        p.drawRoundedRect(QRectF(cx - 6, cy - 3, 8, 9.5), 1.6, 1.6);
        QPainterPath back;
        back.moveTo(cx - 3, cy - 3);
        back.lineTo(cx - 3, cy - 6);
        back.lineTo(cx + 5, cy - 6);
        back.lineTo(cx + 5, cy + 3);
        back.lineTo(cx + 2, cy + 3);
        p.drawPath(back);
    } else {
        p.drawLine(QPointF(cx - 6, cy - 4.5), QPointF(cx + 6, cy - 4.5));
        QPainterPath path;
        path.moveTo(cx - 6, cy);
        path.lineTo(cx + 3, cy);
        path.arcTo(QRectF(cx + 1, cy, 4, 4), 90, -180);
        path.lineTo(cx - 2, cy + 4);
        p.drawPath(path);
        p.drawLine(QPointF(cx - 2, cy + 4), QPointF(cx, cy + 2));
        p.drawLine(QPointF(cx - 2, cy + 4), QPointF(cx, cy + 6));
        p.drawLine(QPointF(cx - 6, cy + 4), QPointF(cx - 4, cy + 4));
    }
}

PathBar::PathBar(QWidget *parent)
    : QWidget(parent)
{
    setFont(AppFont::mono());
}

QSize PathBar::sizeHint() const
{
    return QSize(100, QFontMetrics(font()).height() + 12);
}

void PathBar::setPath(const QString &path)
{
    if (path == m_path)
        return;
    m_path = path;
    m_copied = false;
    update();
}

void PathBar::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    update();
}

void PathBar::flashCopied()
{
    m_copied = true;
    update();
    QPointer<PathBar> self(this);
    QTimer::singleShot(1300, this, [self] {
        if (self) {
            self->m_copied = false;
            self->update();
        }
    });
}

void PathBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor text = palette().color(QPalette::Text);
    const QColor dim = palette().color(QPalette::PlaceholderText);

    QColor line = text;
    line.setAlpha(36);
    p.fillRect(0, 0, width(), 1, line);
    if (m_busy)
        return;

    const QFontMetrics fm(font());
    QFont small = font();
    small.setPointSizeF(qMax(7.0, font().pointSizeF() - 1.5));
    const QFontMetrics sfm(small);

    // Only the "Copied" confirmation uses the right-hand side; the rest of the strip is for the path
    const QString hint = m_copied ? tr("Copied") : QString();
    const int hintWidth = hint.isEmpty() ? 0 : sfm.horizontalAdvance(hint) + 12;

    const QRect area = rect().adjusted(12, 1, -12 - hintWidth, 0);
    if (m_path.isEmpty()) {
        p.setPen(dim);
        p.drawText(area, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(tr("Hover a line to see its path"), Qt::ElideRight, area.width()));
        return;
    }
    p.setPen(text);
    p.drawText(area, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_path, Qt::ElideMiddle, area.width()));

    p.setFont(small);
    p.setPen(m_copied ? palette().color(QPalette::Text) : dim);
    p.drawText(rect().adjusted(0, 1, -12, 0), Qt::AlignVCenter | Qt::AlignRight, hint);
}

namespace {

constexpr int kFrameMs = 16;
constexpr qreal kStartSpeed = 220; // px/s needed to glide at all
constexpr qreal kStopSpeed = 20;   // px/s at which the glide ends
constexpr qreal kMaxSpeed = 10000; // px/s
constexpr qreal kTau = 0.7;        // seconds for the speed to fall to 1/e

}

KineticScroll::KineticScroll(QAbstractScrollArea *area)
    : QObject(area)
    , m_area(area)
    , m_edit(qobject_cast<QPlainTextEdit *>(area) ? dynamic_cast<FoldingEdit *>(area) : nullptr)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(55);
    m_frame.setInterval(kFrameMs);
    connect(&m_idle, &QTimer::timeout, this, &KineticScroll::startGlide);
    connect(&m_frame, &QTimer::timeout, this, &KineticScroll::glide);
    area->viewport()->installEventFilter(this);
}

// QTextBrowser scrolls by pixel already; FoldingEdit has a pixel position of its own.
// Any other QPlainTextEdit would move a whole line per step (not used here).
qreal KineticScroll::unitPx() const
{
    return qobject_cast<QPlainTextEdit *>(m_area) && !m_edit ? qMax(1, m_area->fontMetrics().lineSpacing()) : 1;
}

qreal KineticScroll::position() const
{
    return m_edit ? m_edit->scrollPixels() : m_area->verticalScrollBar()->value() * unitPx();
}

qreal KineticScroll::limit() const
{
    return m_edit ? m_edit->maxScrollPixels() : m_area->verticalScrollBar()->maximum() * unitPx();
}

void KineticScroll::moveTo(qreal pixels)
{
    pixels = qBound<qreal>(0, pixels, limit());
    if (m_edit)
        m_edit->setScrollPixels(pixels);
    else
        m_area->verticalScrollBar()->setValue(qRound(pixels / unitPx()));
}

bool KineticScroll::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_area->viewport())
        return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::Wheel)
        return wheel(static_cast<QWheelEvent *>(event));
    return false;
}

bool KineticScroll::wheel(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
        return false; // zoom
    if (event->phase() == Qt::ScrollMomentum)
        return true; // the platform already glides (macOS); do not add ours on top

    // Touchpads report pixel deltas, or small angle deltas; a mouse wheel notch is 120
    const QPoint pixel = event->pixelDelta();
    const int angle = event->angleDelta().y();
    const bool touchpad = !pixel.isNull() || (angle != 0 && qAbs(angle) < 120);
    if (!touchpad)
        return false;
    const qreal dy = !pixel.isNull() ? pixel.y() : angle / 120.0 * 45;
    const qreal dx = !pixel.isNull() ? pixel.x() : event->angleDelta().x() / 120.0 * 40;
    if (qAbs(dx) > qAbs(dy))
        return false; // sideways: the view handles it

    m_frame.stop();

    const qreal move = -dy; // wheel up = content moves down = smaller position
    const qreal dt = m_last.isValid() ? qMax<qreal>(4, m_last.nsecsElapsed() / 1e6) : 16;
    const bool fresh = !m_last.isValid() || m_last.elapsed() > 120;
    const qreal instant = move / dt * 1000.0;
    m_velocity = fresh ? 0 : m_velocity * 0.55 + instant * 0.45;
    m_last.restart();

    moveTo(position() + move);
    m_idle.start();
    event->accept();
    return true;
}

void KineticScroll::startGlide()
{
    m_velocity = qBound(-kMaxSpeed, m_velocity, kMaxSpeed);
    if (qAbs(m_velocity) < kStartSpeed) {
        m_velocity = 0; // slow stop: it stays exactly where it is
        return;
    }
    m_clock.start();
    m_frame.start();
}

void KineticScroll::glide()
{
    const qreal dt = qMin<qreal>(0.05, m_clock.restart() / 1000.0);
    const qreal before = position();
    moveTo(before + m_velocity * dt);
    m_velocity *= qExp(-dt / kTau);
    const bool wall = qFuzzyCompare(before + 1, position() + 1);
    if (qAbs(m_velocity) < kStopSpeed || wall) {
        m_frame.stop();
        m_velocity = 0;
    }
}
