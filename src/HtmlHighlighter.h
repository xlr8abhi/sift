#pragma once

#include <QColor>
#include <QHash>
#include <QTextCharFormat>
#include <QSyntaxHighlighter>

// Highlights HTML / XML: tags, attributes, values, comments, doctype, entities.
// Attach with setDocument(); detach with setDocument(nullptr). Only one highlighter
// may be attached to a document at a time, or they overwrite each other's colours.
// Block states carry multi-line comments, quoted values and <script>/<style> bodies. The bodies of
// <script> and <style> are coloured as JavaScript and CSS (strings, numbers, comments, keywords,
// regex literals / selectors, properties, values), including their multi-line comments and templates.
class HtmlHighlighter : public QSyntaxHighlighter
{
public:
    explicit HtmlHighlighter(QTextDocument *document);

    void refreshColors();

protected:
    void highlightBlock(const QString &text) override;

private:
    void loadColors();
    // A QTextCharFormat per colour, built once: setFormat(.., QColor) allocates one for every call
    const QTextCharFormat &fmt(const QColor &color);
    void entities(const QString &text, int from, int to);
    // Colour text[from, to) as JavaScript / CSS. `sub` is the state carried between lines:
    // JS: 0 code, 1 inside /* */, 2 inside a template literal.
    // CSS: bit 0 inside /* */, the rest the brace depth.
    void highlightJs(const QString &text, int from, int to, int &sub);
    void highlightCss(const QString &text, int from, int to, int &sub);

    QColor m_tag;
    QColor m_attr;
    QColor m_value;
    QColor m_doctype;
    QColor m_entity;
    QColor m_dim;
    QColor m_accent;
    QColor m_purple;
    QHash<QRgb, QTextCharFormat> m_formats;
};
