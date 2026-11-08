#include "FoldingEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QPen>
#include <QPainter>
#include <QPainterPath>
#include <QHelpEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QTextBlock>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

class FoldingEdit::Gutter : public QWidget
{
public:
    explicit Gutter(FoldingEdit *edit)
        : QWidget(edit)
        , m_edit(edit)
    {
        setCursor(Qt::ArrowCursor);
    }

    QSize sizeHint() const override { return QSize(m_edit->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_edit->paintGutter(event); }

    void mousePressEvent(QMouseEvent *event) override
    {
        const int block = m_edit->cursorForPosition(QPoint(0, int(event->position().y() + m_edit->m_shift))).blockNumber();
        if (m_edit->m_end.contains(block))
            m_edit->toggle(block);
    }

private:
    FoldingEdit *m_edit;
};

FoldingEdit::FoldingEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_gutter(new Gutter(this))
{
    m_gutter->hide();
    viewport()->setMouseTracking(true);
    m_pathTimer.setSingleShot(true);
    m_pathTimer.setInterval(120);
    connect(&m_pathTimer, &QTimer::timeout, this, [this] {
        updatePathUnderMouse();
        m_scrolling = false;
        if (m_onScrolling)
            m_onScrolling(false);
    });
    m_tipTimer.setSingleShot(true);
    m_tipTimer.setInterval(120); // Qt's default is 700 ms; closing is left untouched
    connect(&m_tipTimer, &QTimer::timeout, this, [this] {
        if (m_hoverBlock >= 0)
            QToolTip::showText(QCursor::pos(), m_hoverText, viewport());
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        clearHover();
        pathUnderMouse();
        // scrolled by something else (keys, search, drag): the sub-line offset no longer applies
        if (!m_settingPixels && m_shift != 0) {
            m_shift = 0;
            viewport()->update();
        }
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { reportPath(textCursor().blockNumber()); });
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    // The base class works out which rectangle to repaint from its whole-line offset, so with the text
    // resting between lines (m_shift) that rectangle is off by a few pixels and only a strip of a
    // changed selection was redrawn. Repaint the whole viewport then.
    auto repaintAll = [this] {
        if (m_shift != 0) {
            viewport()->update();
            m_gutter->update();
        }
    };
    connect(this, &QPlainTextEdit::selectionChanged, this, repaintAll);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, repaintAll);
    connect(this, &QPlainTextEdit::updateRequest, this, [this, repaintAll](const QRect &rect, int dy) {
        repaintAll();
        if (dy)
            m_gutter->scroll(0, dy);
        else
            m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    });
}

int FoldingEdit::gutterWidth() const
{
    if (!m_enabled)
        return 0;
    const QFontMetrics fm(font());
    const int digits = QString::number(qMax(1, blockCount())).size();
    return 8 + fm.horizontalAdvance(QLatin1Char('9')) * digits + 6 + fm.height() + 4;
}

void FoldingEdit::updateGutterWidth()
{
    const int w = gutterWidth();
    setViewportMargins(w, 0, 0, 0);
    m_gutter->setVisible(w > 0);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), w, cr.height()));
}

void FoldingEdit::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    // Ctrl+scroll zoom changes the font: the gutter has to resize with it
    if (event->type() == QEvent::FontChange) {
        updateGutterWidth();
        m_gutter->update();
        viewport()->update();
    }
}

void FoldingEdit::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateGutterWidth();
}

void FoldingEdit::resetFolds(bool enable)
{
    m_enabled = enable;
    m_collapsed.clear();
    m_end.clear();
    m_pills.clear();
    m_counts.clear();
    m_nodes.clear();
    m_nodeOf.clear();
    m_pathBlock = -1;
    m_pathTimer.stop();
    if (m_scrolling) {
        m_scrolling = false;
        if (m_onScrolling)
            m_onScrolling(false);
    }
    clearHover();
    if (enable)
        computeFolds();
    updateGutterWidth();
    m_gutter->update();
    if (!m_lastPath.isEmpty()) {
        m_lastPath.clear();
        if (m_onPath)
            m_onPath(m_lastPath);
    }
}

namespace {

// "city" -> .city   "a b" -> ["a b"]
QString keySegment(const QString &line)
{
    int i = 0;
    while (i < line.size() && line.at(i) == QLatin1Char(' '))
        ++i;
    if (i >= line.size() || line.at(i) != QLatin1Char('"'))
        return {};
    int end = i + 1;
    bool escaped = false;
    while (end < line.size() && line.at(end) != QLatin1Char('"')) {
        if (line.at(end) == QLatin1Char('\\')) {
            escaped = true;
            end += 2;
        } else {
            ++end;
        }
    }
    if (end >= line.size())
        return {};
    QString key = line.mid(i + 1, end - i - 1);
    if (escaped) {
        const QJsonDocument doc = QJsonDocument::fromJson(("[" + line.mid(i, end - i + 1) + "]").toUtf8());
        if (doc.isArray() && doc.array().size() == 1)
            key = doc.array().first().toString();
    }
    static const QRegularExpression ident(QStringLiteral("^[A-Za-z_$][A-Za-z0-9_$]*$"));
    if (ident.match(key).hasMatch())
        return QLatin1Char('.') + key;
    key.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("[\"") + key + QStringLiteral("\"]");
}

}

void FoldingEdit::computeFolds()
{
    struct Frame { int start; bool isArray; int next; int node; };
    QList<Frame> stack;
    m_nodeOf = QList<int>(document()->blockCount(), -1);

    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const QString line = b.text();
        const QString text = line.trimmed();
        if (text.isEmpty())
            continue;
        const int number = b.blockNumber();
        const bool closer = text.startsWith('}') || text.startsWith(']');
        if (closer && !stack.isEmpty()) {
            const Frame frame = stack.takeLast();
            m_nodeOf[number] = frame.node;
            if (number > frame.start + 1) // an empty {} / [] has nothing to hide
                m_end.insert(frame.start, number);
        } else if (!closer) {
            int node;
            if (stack.isEmpty()) {
                node = m_nodes.size();
                m_nodes.append({-1, QString()});
            } else {
                Frame &top = stack.last();
                const QString segment = top.isArray ? QStringLiteral("[%1]").arg(top.next++) : keySegment(line);
                node = m_nodes.size();
                m_nodes.append({top.node, segment});
            }
            m_nodeOf[number] = node;
            if (text.endsWith('{') || text.endsWith('['))
                stack.append({number, text.endsWith('['), 0, node});
        }
    }
}

QString FoldingEdit::pathOfBlock(int block) const
{
    if (block < 0 || block >= m_nodeOf.size())
        return {};
    QStringList parts;
    for (int node = m_nodeOf.at(block); node >= 0; node = m_nodes.at(node).parent)
        parts.prepend(m_nodes.at(node).segment);
    QString path = parts.join(QString());
    if (path.startsWith(QLatin1Char('.')))
        path.remove(0, 1);
    return path;
}

void FoldingEdit::reportPath(int block)
{
    if (!m_enabled || block == m_pathBlock)
        return;
    m_pathBlock = block;
    const QString path = pathOfBlock(block);
    if (path == m_lastPath)
        return;
    m_lastPath = path;
    if (m_onPath)
        m_onPath(path);
}

QString FoldingEdit::countText(int count, bool isArray)
{
    return isArray ? (count == 1 ? tr("1 item") : tr("%1 items").arg(count))
                   : (count == 1 ? tr("1 key") : tr("%1 keys").arg(count));
}

// Items are the lines one level deeper that open a value; the closing brackets
// of nested containers sit at that same depth and are skipped.
int FoldingEdit::countChildren(int start, int indent) const
{
    if (const auto it = m_counts.constFind(start); it != m_counts.constEnd())
        return it.value();
    const int end = m_end.value(start, -1);
    if (end < 0)
        return 0;
    const int itemIndent = indent + 4;
    int count = 0;
    QTextBlock b = document()->findBlockByNumber(start + 1);
    for (int line = start + 1; line < end && b.isValid(); ++line, b = b.next()) {
        const QString t = b.text();
        int i = 0;
        while (i < t.size() && t.at(i) == QLatin1Char(' '))
            ++i;
        if (i == itemIndent && i < t.size() && t.at(i) != QLatin1Char('}') && t.at(i) != QLatin1Char(']'))
            ++count;
    }
    const_cast<FoldingEdit *>(this)->m_counts.insert(start, count);
    return count;
}

void FoldingEdit::applyVisibility()
{
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
        b.setVisible(true);

    QList<int> starts = m_collapsed.values();
    std::sort(starts.begin(), starts.end());
    for (int start : starts) {
        QTextBlock b = document()->findBlockByNumber(start + 1);
        for (int line = start + 1; line <= m_end.value(start) && b.isValid(); ++line, b = b.next())
            b.setVisible(false);
    }

    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
    m_gutter->update();
}

void FoldingEdit::toggle(int startBlock)
{
    if (!m_end.contains(startBlock))
        return;
    if (!m_collapsed.remove(startBlock))
        m_collapsed.insert(startBlock);
    applyVisibility();
}

void FoldingEdit::revealBlock(int block)
{
    bool changed = false;
    for (auto it = m_collapsed.begin(); it != m_collapsed.end();) {
        if (block > *it && block <= m_end.value(*it)) {
            it = m_collapsed.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        applyVisibility();
}

void FoldingEdit::paintGutter(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.setFont(font());
    painter.fillRect(event->rect(), palette().color(QPalette::Window));

    QColor dim = palette().color(QPalette::PlaceholderText);
    const QFontMetrics fm(font());
    const int numberRight = m_gutter->width() - fm.height() - 4 - 6;

    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(origin()).top();
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal height = blockBoundingRect(block).height();
        if (block.isVisible() && top + height >= event->rect().top()) {
            const int n = block.blockNumber();
            painter.setPen(dim);
            painter.drawText(QRectF(0, top, numberRight, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(n + 1));

            if (m_end.contains(n)) {
                const bool collapsed = m_collapsed.contains(n);
                const qreal size = fm.height() * 0.32;
                const qreal cx = m_gutter->width() - fm.height() / 2.0 - 2;
                const qreal cy = top + fm.height() / 2.0;
                QPainterPath path;
                if (collapsed) {
                    path.moveTo(cx - size * 0.6, cy - size);
                    path.lineTo(cx + size * 0.9, cy);
                    path.lineTo(cx - size * 0.6, cy + size);
                } else {
                    path.moveTo(cx - size, cy - size * 0.6);
                    path.lineTo(cx + size, cy - size * 0.6);
                    path.lineTo(cx, cy + size * 0.9);
                }
                path.closeSubpath();
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.fillPath(path, dim);
                painter.setRenderHint(QPainter::Antialiasing, false);
            }
        }
        top += height;
        block = block.next();
    }
}

// Drawn here rather than by QPlainTextEdit, which can only start at a whole line. Everything is
// shifted up by m_shift pixels (less than one line) so the view can scroll smoothly.
void FoldingEdit::setMatches(const QList<QPair<int, int>> &ranges, int current)
{
    m_matches = ranges;
    m_currentMatch = current;
    viewport()->update();
}

// Rectangles (viewport coordinates) of a match: one per wrapped line it touches
QList<QRectF> FoldingEdit::matchRects(const QPair<int, int> &range) const
{
    QList<QRectF> out;
    const QFontMetricsF fm(font());
    // a match may run over several lines of text (and each of those over several wrapped lines)
    for (QTextBlock block = document()->findBlock(range.first); block.isValid() && block.position() < range.second;
         block = block.next()) {
        if (!block.isVisible())
            continue;
        const qreal top = blockBoundingGeometry(block).translated(origin()).top();
        if (top > viewport()->height())
            break;
        if (top + blockBoundingRect(block).height() < 0)
            continue;
        const QTextLayout *layout = block.layout();
        const int start = qMax(0, range.first - block.position());
        const int end = qMin(range.second - block.position(), block.length() - 1);
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int from = qMax(start, line.textStart());
            const int to = qMin(end, line.textStart() + line.textLength());
            if (from >= to)
                continue;
            const qreal x1 = line.cursorToX(from);
            const qreal x2 = line.cursorToX(to);
            // like a 2px-padded inline box: as tall as the font plus 4px, not the whole line
            const qreal boxTop = top + line.y() + line.ascent() - fm.ascent() - 2;
            out.append(QRectF(contentOffset().x() + qMin(x1, x2) - 2, boxTop, qAbs(x2 - x1) + 4, fm.height() + 4));
        }
    }
    return out;
}

// A match as one shape: the union of its per-line boxes, so a match that wraps over several lines is
// one outline instead of overlapping rings, and translucent fills do not double up where boxes touch.
static QPainterPath matchShape(const QList<QRectF> &rects, qreal radius)
{
    QPainterPath shape;
    for (const QRectF &r : rects) {
        QPainterPath one;
        one.addRoundedRect(r, radius, radius);
        shape = shape.united(one);
    }
    return shape;
}

// current=false: the fills of every visible match (drawn under the text);
// current=true: ring and glow of the current one (drawn over it).
// The numbers follow HTTPie's CSS (fill alpha .5 dark / .25 light, .25 / .12 for the current one, radius 4,
// ring and glow), but the colour is the theme's accent (QPalette::Highlight), not HTTPie's blue.
void FoldingEdit::paintMatches(QPainter &painter, bool current)
{
    if (m_matches.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    const QColor accent = palette().color(QPalette::Highlight); // the theme's accent, so every theme gets its own
    constexpr qreal kRadius = 4;
    constexpr qreal kRing = 2.5; // HTTPie's is 2px on a 14px font; a touch more reads the same at our sizes
    constexpr int kGlow = 10;

    if (!current) {
        const int first = firstVisibleBlock().position();
        auto it = std::lower_bound(m_matches.begin(), m_matches.end(), first,
                                   [](const QPair<int, int> &m, int pos) { return m.second < pos; });
        QColor soft = accent;
        soft.setAlpha(dark ? 128 : 64);
        QColor selected = accent;
        selected.setAlpha(dark ? 64 : 31);
        painter.setPen(Qt::NoPen);
        for (; it != m_matches.end(); ++it) {
            const int index = int(it - m_matches.begin());
            const QTextBlock block = document()->findBlock(it->first);
            if (block.isValid() && block.isVisible()
                && blockBoundingGeometry(block).translated(origin()).top() > viewport()->height())
                break;
            painter.setBrush(index == m_currentMatch ? selected : soft);
            painter.drawPath(matchShape(matchRects(*it), kRadius));
        }
    } else if (m_currentMatch >= 0 && m_currentMatch < m_matches.size()) {
        const QPainterPath shape = matchShape(matchRects(m_matches.at(m_currentMatch)), kRadius);
        if (shape.isEmpty()) {
            painter.restore();
            return;
        }
        // the shape grown by `by` pixels in every direction
        auto grown = [&shape](qreal by) {
            QPainterPathStroker stroker;
            stroker.setWidth(2 * by);
            stroker.setJoinStyle(Qt::RoundJoin);
            return stroker.createStroke(shape).united(shape);
        };
        // glow: one-pixel bands around the shape, getting fainter outwards (a blurred shadow outside the box)
        painter.setPen(Qt::NoPen);
        QPainterPath inner = shape;
        for (int d = 1; d <= kGlow; ++d) {
            const QPainterPath outer = grown(d);
            QColor glow = accent;
            const qreal f = 1.0 - (d - 0.5) / kGlow;
            glow.setAlphaF(0.7 * std::pow(f, 1.5));
            painter.setBrush(glow);
            painter.drawPath(outer.subtracted(inner));
            inner = outer;
        }
        // ring: solid band right outside the shape
        painter.setBrush(accent);
        painter.drawPath(grown(kRing).subtracted(shape));
    }
    painter.restore();
}

void FoldingEdit::paintEvent(QPaintEvent *event)
{
    // Qt works out which part to repaint (search matches changing, selection) from the whole-line scroll
    // offset, so while the view rests between two lines the region is off by m_shift pixels and old
    // highlights stay on screen. Repaint everything instead.
    if (m_shift != 0 && event->region().boundingRect() != viewport()->rect())
        viewport()->update();
    m_pills.clear();
    QPainter painter(viewport());
    const QRect area = viewport()->rect();
    painter.fillRect(area, palette().color(QPalette::Base));
    painter.setClipRect(area);

    QPointF offset = origin();
    painter.setBrushOrigin(offset);
    paintMatches(painter, false);
    const QAbstractTextDocumentLayout::PaintContext context = getPaintContext();
    QTextBlock block = firstVisibleBlock();
    while (block.isValid()) {
        const QRectF r = blockBoundingRect(block).translated(offset);
        if (block.isVisible() && r.bottom() >= area.top() && r.top() <= area.bottom()) {
            QList<QTextLayout::FormatRange> selections;
            const int blockStart = block.position();
            const int blockLength = block.length();
            for (const auto &range : context.selections) {
                const int selStart = range.cursor.selectionStart() - blockStart;
                const int selEnd = range.cursor.selectionEnd() - blockStart;
                if (selStart < blockLength && selEnd > 0 && selEnd > selStart) {
                    QTextLayout::FormatRange o;
                    o.start = selStart;
                    o.length = selEnd - selStart;
                    o.format = range.format;
                    selections.append(o);
                } else if (!range.cursor.hasSelection() && range.format.hasProperty(QTextFormat::FullWidthSelection)
                           && block.contains(range.cursor.position())) {
                    QTextLayout::FormatRange o;
                    const QTextLine l = block.layout()->lineForTextPosition(range.cursor.position() - blockStart);
                    o.start = l.textStart();
                    o.length = l.textLength();
                    if (o.start + o.length == blockLength - 1)
                        ++o.length;
                    o.format = range.format;
                    selections.append(o);
                }
            }
            block.layout()->draw(&painter, offset, selections, QRectF(area));
        }
        offset.ry() += r.height();
        if (offset.y() > area.height())
            break;
        block = block.next();
    }

    paintMatches(painter, true);
    if (!m_enabled)
        return;
    paintGuides(painter);

    // Hovered key: a soft band across its line
    if (m_hoverBlock >= 0) {
        const QTextBlock block = document()->findBlockByNumber(m_hoverBlock);
        if (block.isValid() && block.isVisible()) {
            const QRectF rect = blockBoundingGeometry(block).translated(origin());
            QColor band = palette().color(QPalette::Text);
            band.setAlpha(22);
            painter.fillRect(QRectF(0, rect.top(), viewport()->width(), rect.height()), band);
        }
    }

    const QFontMetrics fm(font());

    // How many items the whole document holds, after its first bracket
    const QTextBlock root = document()->begin();
    if (root.isValid() && root.isVisible() && m_end.contains(0) && !m_collapsed.contains(0)) {
        const QString first = root.text();
        const QRectF rect = blockBoundingGeometry(root).translated(origin());
        if (rect.bottom() > 0 && rect.top() < viewport()->height()) {
            const QTextLine line = root.layout()->lineAt(0);
            QColor dim = palette().color(QPalette::PlaceholderText);
            painter.setPen(dim);
            QFont small = font();
            small.setPointSizeF(qMax(7.0, font().pointSizeF() - 1.5));
            painter.setFont(small);
            painter.drawText(QPointF(contentOffset().x() + line.cursorToX(first.size()) + 10,
                                     rect.top() + line.y() + fm.ascent()),
                             countText(countChildren(0, 0), first.trimmed().endsWith('[')));
            painter.setFont(font());
        }
    }

    if (m_collapsed.isEmpty())
        return;

    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = palette().color(QPalette::Text);
    fill.setAlpha(36);
    QColor ink = palette().color(QPalette::Text);
    ink.setAlpha(170);
    const QTextCursor sel = textCursor();

    for (int start : std::as_const(m_collapsed)) {
        const QTextBlock block = document()->findBlockByNumber(start);
        if (!block.isValid() || !block.isVisible())
            continue;
        const QRectF rect = blockBoundingGeometry(block).translated(origin());
        if (rect.bottom() < 0 || rect.top() > viewport()->height())
            continue;

        QTextLayout *layout = block.layout();
        const QTextLine last = layout->lineAt(layout->lineCount() - 1);
        const qreal x = contentOffset().x() + last.cursorToX(block.text().size()) + 4;
        const qreal y = rect.top() + last.y();

        const QString closer = document()->findBlockByNumber(m_end.value(start)).text().trimmed();
        int indent = 0;
        while (indent < block.text().size() && block.text().at(indent) == QLatin1Char(' '))
            ++indent;
        const int children = countChildren(start, indent);
        const QString label = children > 0 ? QStringLiteral("… %1").arg(children) : QStringLiteral("…");
        const QRectF pill(x, y + 1, fm.horizontalAdvance(label) + 10, fm.height() - 2);
        const qreal right = pill.right() + 4 + fm.horizontalAdvance(closer);

        // The hidden lines are part of the selection when it runs past this line's
        // end: extend the selection colour over the "…" marker and the closer too
        const int lineEnd = block.position() + block.length() - 1;
        const bool selected = sel.hasSelection() && sel.selectionStart() <= lineEnd && sel.selectionEnd() > lineEnd;
        if (selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette().color(QPalette::Highlight));
            painter.drawRect(QRectF(x - 4, y, right - x + 4 + 2, fm.height()));
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(selected ? palette().color(QPalette::HighlightedText).darker(100) : fill);
        if (selected) {
            QColor f = palette().color(QPalette::HighlightedText);
            f.setAlpha(50);
            painter.setBrush(f);
        }
        painter.drawRoundedRect(pill, 4, 4);
        painter.setPen(selected ? palette().color(QPalette::HighlightedText) : ink);
        painter.drawText(pill, Qt::AlignCenter, label);
        painter.drawText(QPointF(pill.right() + 4, y + fm.ascent()), closer);
        m_pills.insert(start, pill.toAlignedRect());
    }
}

// Faint vertical lines at every nesting level, so deep keys line up with their parent
void FoldingEdit::paintGuides(QPainter &painter)
{
    const qreal space = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    const qreal left = contentOffset().x() + document()->documentMargin();
    QColor color = palette().color(QPalette::Text);
    color.setAlpha(26);
    painter.setPen(QPen(color, 1));

    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(origin()).top();
    while (block.isValid() && top <= viewport()->height()) {
        const QRectF rect = blockBoundingRect(block);
        if (block.isVisible()) {
            const QString text = block.text();
            int indent = 0;
            while (indent < text.size() && text.at(indent) == QLatin1Char(' '))
                ++indent;
            for (int col = 0; col + 4 <= indent; col += 4) {
                const qreal x = qRound(left + col * space) + 0.5;
                painter.drawLine(QPointF(x, top), QPointF(x, top + rect.height()));
            }
            top += rect.height();
        }
        block = block.next();
    }
}

void FoldingEdit::mousePressEvent(QMouseEvent *event)
{
    // Ctrl+click copies the path of the line that was clicked (like Ctrl+click selecting in Dolphin).
    // The click itself decides the line, so nothing can change under the mouse before it is copied.
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) && m_enabled) {
        const int block = cursorForPosition(event->position().toPoint() + QPoint(0, qRound(m_shift))).blockNumber();
        m_pathTimer.stop();
        if (m_scrolling) {
            m_scrolling = false;
            if (m_onScrolling)
                m_onScrolling(false);
        }
        reportPath(block);
        const QString path = pathOfBlock(block);
        if (!path.isEmpty() && m_onCopyPath)
            m_onCopyPath(path);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        for (auto it = m_pills.cbegin(); it != m_pills.cend(); ++it) {
            if (it.value().contains(event->position().toPoint())) {
                toggle(it.key());
                return;
            }
        }
    }
    const QMouseEvent moved = shifted(event);
    QPlainTextEdit::mousePressEvent(const_cast<QMouseEvent *>(&moved));
}

int FoldingEdit::containerCountAt(const QPoint &pos, int *outBlock, bool *outIsArray) const
{
    if (!m_enabled)
        return -1;
    const QTextCursor cursor = cursorForPosition(pos + QPoint(0, qRound(m_shift)));
    const QTextBlock block = cursor.block();
    const QString text = block.text();

    // Only lines of the form   "key": [   or   "key": []   (pretty-printed output)
    int indent = 0;
    while (indent < text.size() && text.at(indent) == QLatin1Char(' '))
        ++indent;
    if (indent >= text.size() || text.at(indent) != QLatin1Char('"'))
        return -1;
    int keyEnd = indent + 1; // index of the closing quote
    while (keyEnd < text.size() && text.at(keyEnd) != QLatin1Char('"'))
        keyEnd += text.at(keyEnd) == QLatin1Char('\\') ? 2 : 1;
    if (keyEnd + 2 >= text.size() || text.at(keyEnd + 1) != QLatin1Char(':'))
        return -1;

    // Only the key itself (quotes included) reacts, not the rest of the line
    const int col = cursor.positionInBlock();
    if (col < indent || col > keyEnd + 1)
        return -1;

    QString rest = text.mid(keyEnd + 2).trimmed();
    if (rest.endsWith(QLatin1Char(',')))
        rest.chop(1);
    const bool isArray = rest == QLatin1String("[") || rest == QLatin1String("[]");
    const bool isObject = rest == QLatin1String("{") || rest == QLatin1String("{}");
    if (!isArray && !isObject)
        return -1;
    if (outBlock)
        *outBlock = block.blockNumber();
    if (outIsArray)
        *outIsArray = isArray;
    if (rest.size() == 2)
        return 0;

    if (m_end.value(block.blockNumber(), -1) < 0)
        return -1;
    return countChildren(block.blockNumber(), indent);
}

bool FoldingEdit::viewportEvent(QEvent *event)
{
    // The tooltip is driven by m_tipTimer (shorter opening delay than Qt's default)
    if (event->type() == QEvent::ToolTip)
        return true;
    return QPlainTextEdit::viewportEvent(event);
}

void FoldingEdit::clearHover()
{
    m_tipTimer.stop();
    QToolTip::hideText();
    if (m_hoverBlock != -1) {
        m_hoverBlock = -1;
        viewport()->update();
    }
}

void FoldingEdit::mouseMoveEvent(QMouseEvent *event)
{
    {
        const QMouseEvent moved = shifted(event);
        QPlainTextEdit::mouseMoveEvent(const_cast<QMouseEvent *>(&moved));
    }
    if (event->buttons() != Qt::NoButton) {
        clearHover();
        // Dragging a selection to the end of a collapsed line takes the whole
        // collapsed block with it (hidden lines and the closer), so what is
        // highlighted, what is copied and what the marker shows all agree.
        QTextCursor cur = textCursor();
        if (m_enabled && cur.hasSelection() && cur.position() > cur.anchor()) {
            const QTextBlock block = cur.block();
            if (m_collapsed.contains(block.blockNumber()) && cur.positionInBlock() == block.text().size()) {
                const QTextBlock closer = document()->findBlockByNumber(m_end.value(block.blockNumber()));
                if (closer.isValid()) {
                    cur.setPosition(closer.position() + closer.text().size(), QTextCursor::KeepAnchor);
                    setTextCursor(cur);
                }
            }
        }
        // Same for a drag that starts at the end of a collapsed line and goes
        // backwards (right to left, or upwards): move the anchor past the block
        if (m_enabled && cur.hasSelection() && cur.position() < cur.anchor()) {
            const QTextBlock block = document()->findBlock(cur.anchor());
            if (m_collapsed.contains(block.blockNumber()) && cur.anchor() == block.position() + block.text().size()) {
                const QTextBlock closer = document()->findBlockByNumber(m_end.value(block.blockNumber()));
                if (closer.isValid()) {
                    const int pos = cur.position();
                    cur.setPosition(closer.position() + closer.text().size());
                    cur.setPosition(pos, QTextCursor::KeepAnchor);
                    setTextCursor(cur);
                }
            }
        }
        return;
    }
    if (m_enabled)
        reportPath(cursorForPosition(event->position().toPoint() + QPoint(0, qRound(m_shift))).blockNumber());
    int block = -1;
    bool isArray = false;
    const int count = containerCountAt(event->position().toPoint(), &block, &isArray);
    const int hovered = count >= 0 ? block : -1;
    if (hovered == m_hoverBlock)
        return;
    clearHover();
    if (hovered < 0)
        return;
    m_hoverBlock = hovered;
    m_hoverText = countText(count, isArray);
    m_tipTimer.start();
    viewport()->update();
}

void FoldingEdit::leaveEvent(QEvent *event)
{
    QPlainTextEdit::leaveEvent(event);
    clearHover();
}

// The base class maps mouse positions with its own whole-line offset, so a view resting between two
// lines (m_shift) needs the position moved by that much before the base class sees it.
QMouseEvent FoldingEdit::shifted(const QMouseEvent *event) const
{
    const QPointF by(0, m_shift);
    return QMouseEvent(event->type(), event->position() + by, event->scenePosition() + by, event->globalPosition(),
                       event->button(), event->buttons(), event->modifiers(), event->pointingDevice());
}

void FoldingEdit::mouseReleaseEvent(QMouseEvent *event)
{
    const QMouseEvent moved = shifted(event);
    QPlainTextEdit::mouseReleaseEvent(const_cast<QMouseEvent *>(&moved));
}

void FoldingEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QMouseEvent moved = shifted(event);
    QPlainTextEdit::mouseDoubleClickEvent(const_cast<QMouseEvent *>(&moved));
}

QPointF FoldingEdit::origin() const
{
    return contentOffset() - QPointF(0, m_shift);
}

qreal FoldingEdit::lineHeight() const
{
    const QTextBlock first = document()->firstBlock();
    if (first.isValid() && first.layout() && first.layout()->lineCount() > 0) {
        const qreal h = first.layout()->lineAt(0).height();
        if (h > 1)
            return h;
    }
    return qMax(1, fontMetrics().lineSpacing());
}

qreal FoldingEdit::scrollPixels() const
{
    return verticalScrollBar()->value() * lineHeight() + m_shift;
}

qreal FoldingEdit::maxScrollPixels() const
{
    return verticalScrollBar()->maximum() * lineHeight();
}

void FoldingEdit::setScrollPixels(qreal pixels)
{
    QScrollBar *bar = verticalScrollBar();
    const qreal lh = lineHeight();
    pixels = qBound<qreal>(0, pixels, bar->maximum() * lh);
    int line = int(pixels / lh);
    qreal shift = pixels - line * lh;
    if (line >= bar->maximum()) {
        line = bar->maximum();
        shift = 0;
    }
    m_settingPixels = true;
    m_shift = shift;
    bar->setValue(line);
    m_settingPixels = false;
    clearHover();
    viewport()->update();
    m_gutter->update();
    pathUnderMouse();
}

// After a scroll the line under a resting mouse is a different one, though no mouse event says so.
// Only looked at once the scrolling has stopped (every scroll step restarts the timer), not on each frame.
void FoldingEdit::pathUnderMouse()
{
    if (!m_enabled)
        return;
    if (!m_scrolling) {
        m_scrolling = true;
        if (m_onScrolling)
            m_onScrolling(true); // the path bar goes blank while the text is moving
    }
    m_pathTimer.start();
}

void FoldingEdit::updatePathUnderMouse()
{
    if (!m_enabled || !viewport()->underMouse())
        return;
    const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    if (viewport()->rect().contains(pos))
        reportPath(cursorForPosition(pos + QPoint(0, qRound(m_shift))).blockNumber());
}

void FoldingEdit::scrollContentsBy(int dx, int dy)
{
    QPlainTextEdit::scrollContentsBy(dx, dy);
    viewport()->update(); // repaint everything: the shifted drawing cannot reuse scrolled pixels
    m_gutter->update();
}
