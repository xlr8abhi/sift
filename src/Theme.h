#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPalette>
#include <QString>

struct Theme
{
    QString name;
    bool dark = true;
    bool system = false;
    QMap<QString, QColor> c30;
    QMap<QString, QColor> c16;

    QColor color(const QString &key, const QColor &fallback = QColor()) const;
};

struct SemanticColors
{
    QColor ok;
    QColor warn;
    QColor err;
    QColor accent;
    QColor purple;
    QColor orange;
    QColor key;
    QColor string;
    QColor number;
    QColor keyword;
};

QColor methodColor(const QString &method);

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager &instance();

    const QList<Theme> &all();
    const Theme &current() const { return m_current; }
    SemanticColors colors() const;

    void apply(const Theme &theme);
    void commit(const Theme &theme);
    void restoreSaved();

    static QString systemName() { return QStringLiteral("System (KDE)"); }

signals:
    void changed();

private:
    ThemeManager();

    QStringList directories() const;
    Theme parseFile(const QString &path) const;
    Theme makeSystemTheme() const;
    QPalette buildPalette(const Theme &theme) const;

    static QString paletteStyleName();

    QPalette m_systemPalette;
    QString m_systemStyle;
    QList<Theme> m_all;
    bool m_loaded = false;
    Theme m_current;
};
