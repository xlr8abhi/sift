#pragma once

#include <QHash>
#include <QMouseEvent>
#include <QList>
#include <QPair>
#include <QString>
#include <functional>
#include <QPlainTextEdit>
#include <QRect>
#include <QSet>
#include <QTimer>

// Read-only viewer for pretty-printed JSON with a line-number gutter and
// collapsible {...} / [...] blocks. Folding is computed from the text itself.
class FoldingEdit : public QPlainTextEdit
{
public:
    explicit FoldingEdit(QWidget *parent = nullptr);

    // Call after every setPlainText(). With enable=false the gutter is hidden.
    void resetFolds(bool enable);
    // Expands any collapsed block that hides the given line.
    void revealBlock(int block);
    // Smooth scrolling: position in pixels from the top, finer than the scroll bar's whole lines.
    // Used by KineticScroll; the text is drawn shifted by the part below one line.
    qreal scrollPixels() const;
    qreal maxScrollPixels() const;
    qreal lineHeight() const;
    void setScrollPixels(qreal pixels);
    bool foldingEnabled() const { return m_enabled; }
    // Search matches as (start, end) text positions, sorted. The current one gets an outline and a glow.
    // Painted by this class (rounded fill under the text, so syntax colours stay), not as extra selections.
    void setMatches(const QList<QPair<int, int>> &ranges, int current);
    // JSON path of the line the mouse or text cursor is on, like "[0].address.city".
    // The callback fires whenever it changes (empty string when there is none).
    QString currentPath() const { return m_lastPath; }
    void setPathCallback(std::function<void(const QString &)> callback) { m_onPath = std::move(callback); }
    // Called with the path of a line that was Ctrl+clicked
    void setCopyPathCallback(std::function<void(const QString &)> callback) { m_onCopyPath = std::move(callback); }
    // Called with true when the text starts scrolling and false once it has stopped
    void setScrollingCallback(std::function<void(bool)> callback) { m_onScrolling = std::move(callback); }

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    class Gutter;

    void paintGutter(QPaintEvent *event);
    int gutterWidth() const;
    void updateGutterWidth();
    void toggle(int startBlock);
    void applyVisibility();
    void computeFolds();
    // Number of top-level items of the array / object whose key is under viewport
    // point pos, or -1 when pos is not on such a key. Fills block and isArray.
    int containerCountAt(const QPoint &pos, int *block = nullptr, bool *isArray = nullptr) const;
    void clearHover();
    QMouseEvent shifted(const QMouseEvent *event) const; // position moved by m_shift, for the base class
    QPointF origin() const; // contentOffset() moved up by the sub-line scroll
    int countChildren(int block, int indent) const;
    static QString countText(int count, bool isArray);
    QString pathOfBlock(int block) const;
    void reportPath(int block);
    void pathUnderMouse();       // schedules updatePathUnderMouse() for when scrolling has stopped
    void updatePathUnderMouse();
    void paintGuides(QPainter &painter);
    QList<QRectF> matchRects(const QPair<int, int> &range) const;
    void paintMatches(QPainter &painter, bool current);

    Gutter *m_gutter;
    bool m_enabled = false;
    QList<QPair<int, int>> m_matches;
    int m_currentMatch = -1;
    QHash<int, int> m_end;       // fold start line -> closing line
    QSet<int> m_collapsed;       // start lines currently collapsed
    int m_hoverBlock = -1;       // line whose key is hovered (highlighted), or -1
    QString m_hoverText;         // tooltip text pending for that line
    QTimer m_pathTimer;          // fires 120 ms after the last scroll step
    QTimer m_tipTimer;           // short opening delay of the tooltip
    QHash<int, int> m_counts;    // fold start line -> number of top-level children (cache)
    struct PathNode { int parent; QString segment; };
    QList<PathNode> m_nodes;     // one per element line, linked to its parent container
    QList<int> m_nodeOf;         // block number -> index in m_nodes, or -1
    int m_pathBlock = -1;
    qreal m_shift = 0;           // pixels (less than one line) the text is drawn above its line position
    bool m_settingPixels = false;
    QString m_lastPath;
    std::function<void(const QString &)> m_onPath;
    std::function<void(bool)> m_onScrolling;
    std::function<void(const QString &)> m_onCopyPath;
    bool m_scrolling = false;
    QHash<int, QRect> m_pills;   // "…" markers from the last paint
};
