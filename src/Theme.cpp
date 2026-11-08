#include "Theme.h"
#include "Style.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>

namespace {

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

QMap<QString, QColor> parseTable(const QString &text, const QString &table, const QMap<QString, QColor> &ref)
{
    QMap<QString, QColor> out;
    const QRegularExpression block("M\\." + table + "\\s*=\\s*\\{(.*?)\\n\\}",
                                   QRegularExpression::DotMatchesEverythingOption);
    const auto match = block.match(text);
    if (!match.hasMatch())
        return out;

    static const QRegularExpression entry("(\\w+)\\s*=\\s*([^,\\n]+?)\\s*,");
    static const QRegularExpression hex("^\"(#[0-9a-fA-F]{6})\"");
    static const QRegularExpression reference("^M\\.base_30\\.(\\w+)");

    auto it = entry.globalMatch(match.captured(1));
    while (it.hasNext()) {
        const auto m = it.next();
        const QString value = m.captured(2);
        const auto h = hex.match(value);
        if (h.hasMatch()) {
            out.insert(m.captured(1), QColor(h.captured(1)));
            continue;
        }
        const auto r = reference.match(value);
        if (r.hasMatch() && ref.contains(r.captured(1)))
            out.insert(m.captured(1), ref.value(r.captured(1)));
    }
    return out;
}

}

QColor Theme::color(const QString &key, const QColor &fallback) const
{
    return c30.contains(key) ? c30.value(key) : fallback;
}

QColor methodColor(const QString &method)
{
    const SemanticColors colors = ThemeManager::instance().colors();
    if (method == "GET")
        return colors.ok;
    if (method == "POST")
        return colors.warn;
    if (method == "PUT")
        return colors.accent;
    if (method == "PATCH")
        return colors.purple;
    return colors.err;
}

ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

QString ThemeManager::paletteStyleName()
{
    const QStringList keys = QStyleFactory::keys();
    for (const QString &key : keys) {
        if (key.compare("Breeze", Qt::CaseInsensitive) == 0)
            return key;
    }
    return QStringLiteral("Fusion");
}

ThemeManager::ThemeManager()
    : m_systemPalette(QApplication::palette())
    , m_systemStyle(QApplication::style()->name())
{
    m_current = makeSystemTheme();
}

QStringList ThemeManager::directories() const
{
    return {QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/themes",
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + "/nvim/lazy/base46/lua/base46/themes"};
}

Theme ThemeManager::parseFile(const QString &path) const
{
    Theme theme;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return theme;
    const QString text = QString::fromUtf8(file.readAll());

    theme.name = QFileInfo(path).completeBaseName();
    theme.c30 = parseTable(text, "base_30", {});
    theme.c16 = parseTable(text, "base_16", theme.c30);

    static const QRegularExpression type("M\\.type\\s*=\\s*\"(\\w+)\"");
    const auto m = type.match(text);
    theme.dark = !(m.hasMatch() && m.captured(1) == "light");

    for (const char *key : {"black", "darker_black", "black2", "white", "red", "green", "yellow", "blue"}) {
        if (!theme.c30.contains(key)) {
            theme.name.clear();
            break;
        }
    }
    return theme;
}

Theme ThemeManager::makeSystemTheme() const
{
    Theme theme;
    theme.name = systemName();
    theme.system = true;
    theme.dark = m_systemPalette.color(QPalette::Window).lightness() < 128;
    theme.c30 = {{"black", m_systemPalette.color(QPalette::Base)},
                 {"one_bg3", m_systemPalette.color(QPalette::Button)},
                 {"grey", m_systemPalette.color(QPalette::Mid)},
                 {"light_grey", m_systemPalette.color(QPalette::PlaceholderText)},
                 {"red", QColor("#ff6b7a")},
                 {"orange", QColor("#f3b27a")},
                 {"yellow", QColor("#f0b24a")},
                 {"green", QColor("#3ecf8e")},
                 {"blue", m_systemPalette.color(QPalette::Highlight)},
                 {"purple", QColor("#d49cff")}};
    return theme;
}

const QList<Theme> &ThemeManager::all()
{
    if (m_loaded)
        return m_all;

    QMap<QString, Theme> byName;
    const QStringList dirs = directories();
    for (int i = dirs.size() - 1; i >= 0; --i) {
        const QDir dir(dirs.at(i));
        for (const QString &name : dir.entryList({"*.lua"}, QDir::Files)) {
            Theme theme = parseFile(dir.filePath(name));
            if (!theme.name.isEmpty())
                byName.insert(theme.name, theme);
        }
    }

    m_all.clear();
    m_all.append(makeSystemTheme());
    QList<Theme> sorted = byName.values();
    std::sort(sorted.begin(), sorted.end(), [](const Theme &a, const Theme &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    m_all.append(sorted);
    m_loaded = true;
    return m_all;
}

SemanticColors ThemeManager::colors() const
{
    const Theme &t = m_current;
    SemanticColors c;
    c.ok = t.color("green", QColor("#3ecf8e"));
    c.warn = t.color("yellow", QColor("#f0b24a"));
    c.err = t.color("red", QColor("#ff6b7a"));
    c.accent = t.color("blue", QColor("#5b9dff"));
    c.purple = t.color("purple", QColor("#d49cff"));
    c.orange = t.color("orange", QColor("#f3b27a"));

    if (t.system) {
        const bool darkBase = m_systemPalette.color(QPalette::Base).lightness() < 128;
        if (!darkBase) {
            c.ok = QColor("#1a8a4f");
            c.warn = QColor("#b36b00");
            c.err = QColor("#c62828");
            c.purple = QColor("#8a2be2");
            c.orange = QColor("#b35900");
        }
        c.key = QColor(darkBase ? "#7fb6ff" : "#0b5cad");
        c.string = QColor(darkBase ? "#a5e08a" : "#2a7d1f");
        c.number = QColor(darkBase ? "#f3b27a" : "#b35900");
        c.keyword = QColor(darkBase ? "#d49cff" : "#8a2be2");
    } else {
        c.key = t.c16.value("base0D", c.accent);
        c.string = t.c16.value("base0B", t.color("green", c.ok));
        c.number = t.c16.value("base09", c.orange);
        c.keyword = t.c16.value("base0E", c.purple);
    }
    return c;
}

QPalette ThemeManager::buildPalette(const Theme &t) const
{
    const QColor bg = t.color("black");
    const QColor chrome = t.color("darker_black", bg);
    const QColor alt = t.color("black2", bg);
    const QColor fg = t.color("white");
    const QColor accent = t.color("blue");
    const QColor button = t.color("one_bg2", t.color("one_bg", alt));
    const QColor accentText = accent.lightnessF() > 0.6 ? QColor("#101010") : QColor("#ffffff");

    QPalette p;
    p.setColor(QPalette::Window, chrome);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::BrightText, fg);
    p.setColor(QPalette::ToolTipBase, alt);
    p.setColor(QPalette::ToolTipText, fg);
    p.setColor(QPalette::PlaceholderText, mix(fg, bg, 0.5));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, accentText);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::LinkVisited, t.color("purple", accent));
    p.setColor(QPalette::Light, button.lighter(140));
    p.setColor(QPalette::Midlight, button.lighter(115));
    p.setColor(QPalette::Mid, t.color("line", button));
    p.setColor(QPalette::Dark, chrome.darker(140));
    p.setColor(QPalette::Shadow, QColor("#000000"));

    const QColor muted = mix(fg, bg, 0.6);
    p.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    p.setColor(QPalette::Disabled, QPalette::Text, muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    return p;
}

void ThemeManager::apply(const Theme &theme)
{
    m_current = theme;

    // With a stylesheet installed, changing the style or palette re-resolves it for every widget
    // (seconds with many tabs open). Drop it first; Style::install sets the new one below.
    Style::deferHiddenWidgets();
    qApp->setStyleSheet(QString());

    const QString wanted = theme.system ? m_systemStyle : paletteStyleName();
    if (QApplication::style()->name().compare(wanted, Qt::CaseInsensitive) != 0)
        QApplication::setStyle(wanted);

    QApplication::setPalette(theme.system ? m_systemPalette : buildPalette(theme));
    Style::install();
    emit changed();
}

void ThemeManager::commit(const Theme &theme)
{
    apply(theme);
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
    settings.setValue("theme", theme.name);
}

void ThemeManager::restoreSaved()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "sift", "sift");
    const QString name = settings.value("theme").toString();
    if (name.isEmpty() || name == systemName())
        return;

    for (const QString &dirPath : directories()) {
        const QString path = dirPath + "/" + name + ".lua";
        if (!QFileInfo::exists(path))
            continue;
        const Theme theme = parseFile(path);
        if (!theme.name.isEmpty()) {
            apply(theme);
            return;
        }
    }
}
