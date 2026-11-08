#include "AppFont.h"

#include <QApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QWidget>

namespace {

qreal g_default = 0;
qreal g_saved = 0;
int g_defaultWeight = 0;
int g_savedWeight = 0;

QSettings settings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
}

void apply()
{
    const qreal size = AppFont::effectiveSize();
    const QString monoFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

    const int weight = AppFont::effectiveWeight();
    QFont app = QApplication::font();
    app.setPointSizeF(size);
    app.setWeight(static_cast<QFont::Weight>(weight));
    QApplication::setFont(app);

    // Widgets that were given the monospace font explicitly don't follow the app font.
    for (QWidget *widget : QApplication::allWidgets()) {
        QFont font = widget->font();
        if (font.family() == monoFamily) {
            font.setPointSizeF(size);
            if (!widget->property("sift_bold").toBool())
                font.setWeight(static_cast<QFont::Weight>(weight));
            widget->setFont(font);
        }
    }
}

}

namespace AppFont {

qreal defaultSize()
{
    if (g_default <= 0) {
        g_default = QApplication::font().pointSizeF();
        if (g_default <= 0)
            g_default = 10;
    }
    return g_default;
}

int defaultWeight()
{
    if (g_defaultWeight <= 0)
        g_defaultWeight = QApplication::font().weight() > 0 ? QApplication::font().weight() : 400;
    return g_defaultWeight;
}

int savedWeight()
{
    return g_savedWeight;
}

int effectiveWeight()
{
    return g_savedWeight > 0 ? g_savedWeight : defaultWeight();
}

void setWeight(int weight)
{
    g_savedWeight = qBound(0, weight, 900);
    QSettings s = settings();
    if (g_savedWeight > 0)
        s.setValue("font_weight", g_savedWeight);
    else
        s.remove("font_weight");
    apply();
}

qreal savedSize()
{
    return g_saved;
}

qreal effectiveSize()
{
    return g_saved > 0 ? g_saved : defaultSize();
}

void restoreSaved()
{
    defaultSize();
    defaultWeight();
    g_savedWeight = qBound(0, settings().value("font_weight", 0).toInt(), 900);
    g_saved = qBound(0.0, settings().value("font_size", 0).toDouble(), 40.0);
    if (g_saved > 0 || g_savedWeight > 0)
        apply();
}

void setSize(qreal pt)
{
    g_saved = qBound(0.0, pt, 40.0);
    QSettings s = settings();
    if (g_saved > 0)
        s.setValue("font_size", g_saved);
    else
        s.remove("font_size");
    apply();
}

QFont mono()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(effectiveSize());
    font.setWeight(static_cast<QFont::Weight>(effectiveWeight()));
    return font;
}

}
