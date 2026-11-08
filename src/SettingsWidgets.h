#pragma once

#include <QAbstractButton>
#include <QStringList>
#include <QWidget>

// Small custom-painted controls for the Settings panel. They read colours from the
// widget palette and ThemeManager while painting, so a theme change only needs a repaint.

// On/off switch (a checkable button).
class Switch : public QAbstractButton
{
    Q_OBJECT

public:
    explicit Switch(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// A row of mutually exclusive choices drawn as one rounded box.
class Segmented : public QWidget
{
    Q_OBJECT

public:
    explicit Segmented(const QStringList &labels, QWidget *parent = nullptr);

    void setCurrent(int index); // -1 = none selected
    int current() const { return m_current; }
    QSize sizeHint() const override;

signals:
    void activated(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QStringList m_labels;
    int m_current = -1;
};

// Shows the current theme (colour dots and name); clicking it is up to the owner.
class ThemeRow : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ThemeRow(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hover = false;
};

// Two lines of sample JSON in the app's monospace font and the theme's colours.
class FontPreview : public QWidget
{
    Q_OBJECT

public:
    explicit FontPreview(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
};
