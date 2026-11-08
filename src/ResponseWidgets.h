#pragma once

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWidget>

class FoldingEdit;
class QWheelEvent;

// Small flat icon button for the response status bar. The glyph is painted, so it
// follows the theme and needs no icon files.
class IconButton : public QAbstractButton
{
public:
    enum class Kind { Search, Wrap, Copy };

    explicit IconButton(Kind kind, QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(26, 26); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Kind m_kind;
};

// One line under the response showing the JSON path of the line you are on
// ("[0].address.city"). It is display only: Ctrl+click on a line copies its path.
class PathBar : public QWidget
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    void setPath(const QString &path);
    QString path() const { return m_path; }
    // Blank (nothing drawn) while the text is scrolling
    void setBusy(bool busy);
    // Shows "Copied" at the right end for a moment
    void flashCopied();
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_path;
    bool m_copied = false;
    bool m_busy = false;
};

class QAbstractScrollArea;

// Momentum for touchpad scrolling. Qt's text views stop dead when the fingers lift (the compositor
// sends no momentum events), so this follows the fingers, measures their speed and keeps gliding
// with friction afterwards. Mouse wheel notches and Ctrl+wheel zoom are left to the view.
class KineticScroll : public QObject
{
public:
    explicit KineticScroll(QAbstractScrollArea *area);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool wheel(QWheelEvent *event);
    void startGlide();
    void glide();
    void moveTo(qreal pixels);
    qreal position() const; // scroll position in pixels from the top
    qreal limit() const;    // largest position
    qreal unitPx() const;

    QAbstractScrollArea *m_area;
    FoldingEdit *m_edit; // set when the view can scroll by pixel
    QTimer m_idle;  // no wheel event for a moment: the fingers have lifted
    QTimer m_frame; // glide animation
    QElapsedTimer m_clock;
    QElapsedTimer m_last;
    qreal m_velocity = 0; // pixels per second, positive = down
};
