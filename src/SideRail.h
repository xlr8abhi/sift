#pragma once

#include <QWidget>

class QToolButton;
class QVBoxLayout;

// Thin icon strip on the left edge. Each button selects a sidebar panel.
// Icons are painted from the palette, so they follow every theme.
class SideRail : public QWidget
{
    Q_OBJECT

public:
    enum class Icon { History, Settings };

    explicit SideRail(QWidget *parent = nullptr);

    QToolButton *addButton(Icon icon, const QString &toolTip);
    QToolButton *addBottomButton(Icon icon, const QString &toolTip); // same as addButton, pinned to the bottom

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVBoxLayout *m_layout;
};
