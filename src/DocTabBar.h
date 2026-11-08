#pragma once

#include <QTabBar>
#include <QTabWidget>
#include <QVariantMap>
#include <QWidget>

// Keys of the QVariantMap stored with setTabData() for every document tab.
//   method : "GET", "POST" ...
//   host   : text drawn in the normal colour
//   path   : text drawn after the host in a muted colour
//   fresh  : true for an untouched "New request" tab (drawn muted)
namespace DocTab {
QVariantMap data(const QString &method, const QString &host, const QString &path, bool fresh);
}

// The document tab bar. Qt still owns tab shape, hover and selection colours (the
// application stylesheet); this class paints the tab content itself: a small method
// badge, the title, and a close button that only shows on the current and hovered tab.
class DocTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit DocTabBar(QWidget *parent = nullptr);

    // Width tab i wants, before any shrinking or scrolling
    int preferredWidth(int index) const { return tabSizeHint(index).width(); }

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect closeRect(int index) const;
    bool closeVisible(int index) const;

    int m_hover = -1;
    bool m_hoverClose = false;
};

class DocTabWidget : public QTabWidget
{
    Q_OBJECT

public:
    DocTabWidget() { setTabBar(new DocTabBar(this)); }
};

// One row of the "all tabs" list that the overflow button opens.
class TabListRow : public QWidget
{
    Q_OBJECT

public:
    TabListRow(const QVariantMap &data, bool current, QWidget *parent = nullptr);
    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QVariantMap m_data;
    bool m_current;
    bool m_hover = false;
    bool m_pressed = false;
};
