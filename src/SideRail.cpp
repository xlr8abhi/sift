#include "SideRail.h"

#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

class RailButton : public QToolButton
{
public:
    RailButton(SideRail::Icon icon, QWidget *parent)
        : QToolButton(parent)
        , m_icon(icon)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(42, 38);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (isChecked()) {
            QColor wash = palette().color(QPalette::Highlight);
            wash.setAlpha(45);
            p.fillRect(rect(), wash);
            p.fillRect(QRect(0, 0, 2, height()), palette().color(QPalette::Highlight));
        } else if (underMouse()) {
            QColor wash = palette().color(QPalette::Text);
            wash.setAlpha(22);
            p.fillRect(rect(), wash);
        }

        const QColor ink = (isChecked() || underMouse()) ? palette().color(QPalette::Text)
                                                          : palette().color(QPalette::PlaceholderText);
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.translate(width() / 2.0, height() / 2.0);

        switch (m_icon) {
        case SideRail::Icon::History:
            p.drawEllipse(QPointF(0, 0), 8.0, 8.0);
            p.drawLine(QPointF(0, -4.5), QPointF(0, 0));
            p.drawLine(QPointF(0, 0), QPointF(3.8, 2.4));
            break;
        case SideRail::Icon::Settings:
            p.drawEllipse(QPointF(0, 0), 3.0, 3.0);
            p.drawEllipse(QPointF(0, 0), 6.2, 6.2);
            for (int i = 0; i < 8; ++i) {
                p.save();
                p.rotate(i * 45.0);
                p.drawLine(QPointF(0, -6.2), QPointF(0, -8.6));
                p.restore();
            }
            break;
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        QToolButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QToolButton::leaveEvent(event);
        update();
    }

private:
    SideRail::Icon m_icon;
};

}

SideRail::SideRail(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
{
    setFixedWidth(42);
    m_layout->setContentsMargins(0, 6, 0, 6);
    m_layout->setSpacing(2);
    m_layout->addStretch(1);
}

QToolButton *SideRail::addBottomButton(Icon icon, const QString &toolTip)
{
    auto *button = new RailButton(icon, this);
    button->setToolTip(toolTip);
    m_layout->addWidget(button);
    return button;
}

QToolButton *SideRail::addButton(Icon icon, const QString &toolTip)
{
    auto *button = new RailButton(icon, this);
    button->setToolTip(toolTip);
    m_layout->insertWidget(m_layout->count() - 1, button);
    return button;
}

void SideRail::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(width() - 1, 0, width() - 1, height());
}
