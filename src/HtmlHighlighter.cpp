#include "HtmlHighlighter.h"

#include "Theme.h"

#include <QGuiApplication>
#include <QPalette>
#include <QSet>
#include <QTextCharFormat>

namespace {

enum State { Text = 0, Comment = 1, InTag = 2, RawScript = 3, InDouble = 4, InSingle = 5, RawStyle = 6 };
enum Kind { Other = 0, Script = 1, Style = 2 };

bool isNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == '-' || c == ':' || c == '_' || c == '.';
}

}

HtmlHighlighter::HtmlHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    loadColors();
}

const QTextCharFormat &HtmlHighlighter::fmt(const QColor &color)
{
    auto it = m_formats.find(color.rgba());
    if (it == m_formats.end()) {
        QTextCharFormat format;
        format.setForeground(color);
        it = m_formats.insert(color.rgba(), format);
    }
    return it.value();
}

void HtmlHighlighter::loadColors()
{
    m_formats.clear();
    const SemanticColors c = ThemeManager::instance().colors();
    m_tag = c.keyword;
    m_attr = c.key;
    m_value = c.string;
    m_doctype = c.orange;
    m_entity = c.number;
    m_accent = c.accent;
    m_purple = c.purple;
    m_dim = QGuiApplication::palette().color(QPalette::PlaceholderText);
}

void HtmlHighlighter::refreshColors()
{
    loadColors();
    rehighlight();
}

void HtmlHighlighter::entities(const QString &text, int from, int to)
{
    int i = text.indexOf('&', from);
    while (i >= 0 && i < to) {
        int j = i + 1;
        while (j < to && (text.at(j).isLetterOrNumber() || text.at(j) == '#'))
            ++j;
        if (j < to && text.at(j) == ';' && j > i + 1)
            setFormat(i, j - i + 1, m_entity);
        i = text.indexOf('&', i + 1);
    }
}

void HtmlHighlighter::highlightJs(const QString &t, int from, int to, int &sub)
{
    static const QSet<QString> keywords = {"var", "let", "const", "function", "return", "if", "else", "for", "while", "do",
        "switch", "case", "break", "continue", "new", "delete", "typeof", "instanceof", "in", "of", "void", "throw", "try",
        "catch", "finally", "class", "extends", "super", "import", "export", "default", "from", "as", "async", "await",
        "yield", "static", "get", "set", "with", "debugger"};
    static const QSet<QString> literals = {"true", "false", "null", "undefined", "this", "NaN", "Infinity"};
    static const QSet<QString> beforeRegex = {"return", "typeof", "case", "in", "of", "delete", "void", "throw", "instanceof", "new", "else", "do"};

    QTextCharFormat comment;
    comment.setForeground(m_dim);
    comment.setFontItalic(true);
    auto paint = [this](int start, int end, const QColor &color) { setFormat(start, end - start, fmt(color)); };
    auto closes = [&](int at, QChar quote) {
        // end index (exclusive) of a quoted run starting after `at`, stopping at `to`
        int j = at;
        while (j < to) {
            if (t.at(j) == '\\') {
                j += 2;
                continue;
            }
            if (t.at(j) == quote)
                return j + 1;
            ++j;
        }
        return to;
    };

    int i = from;
    QChar prev = ';'; // last significant character, decides between a regex and a division
    while (i < to) {
        if (sub == 1) {
            const int end = t.indexOf("*/", i);
            if (end < 0 || end + 2 > to) {
                setFormat(i, to - i, comment);
                return;
            }
            setFormat(i, end + 2 - i, comment);
            i = end + 2;
            sub = 0;
            continue;
        }
        if (sub == 2) {
            const int end = closes(i, '`');
            paint(i, end, m_value);
            if (end <= to && end > i && t.at(end - 1) == '`' && !(end - 2 >= i && t.at(end - 2) == '\\')) {
                sub = 0;
                prev = '0';
            }
            i = end;
            continue;
        }

        const QChar c = t.at(i);
        const QChar next = i + 1 < to ? t.at(i + 1) : QChar();
        if (c.isSpace()) {
            ++i;
        } else if (c == '/' && next == '/') {
            setFormat(i, to - i, comment);
            return;
        } else if (c == '/' && next == '*') {
            sub = 1;
        } else if (c == '"' || c == '\'') {
            const int end = closes(i + 1, c);
            paint(i, end, m_value);
            i = end;
            prev = '0';
        } else if (c == '`') {
            paint(i, i + 1, m_value);
            ++i;
            sub = 2;
        } else if (c.isDigit() || (c == '.' && next.isDigit())) {
            int j = i + 1;
            while (j < to && (t.at(j).isLetterOrNumber() || t.at(j) == '.' || t.at(j) == '_'))
                ++j;
            paint(i, j, m_entity);
            i = j;
            prev = '0';
        } else if (c.isLetter() || c == '_' || c == '$') {
            int j = i + 1;
            while (j < to && (t.at(j).isLetterOrNumber() || t.at(j) == '_' || t.at(j) == '$'))
                ++j;
            const QString word = t.mid(i, j - i);
            int k = j;
            while (k < to && t.at(k) == ' ')
                ++k;
            if (keywords.contains(word))
                paint(i, j, m_tag);
            else if (literals.contains(word))
                paint(i, j, m_doctype);
            else if (k < to && t.at(k) == '(')
                paint(i, j, m_accent);
            prev = beforeRegex.contains(word) ? ';' : 'a';
            i = j;
        } else if (c == '/' && QString(";,(=:[!&|?{}+-*%<>~^").contains(prev)) {
            // regex literal: up to the closing slash that is not escaped or inside [...]
            int j = i + 1;
            bool inClass = false;
            bool found = false;
            while (j < to) {
                const QChar d = t.at(j);
                if (d == '\\') {
                    j += 2;
                    continue;
                }
                if (d == '[')
                    inClass = true;
                else if (d == ']')
                    inClass = false;
                else if (d == '/' && !inClass) {
                    found = true;
                    break;
                }
                ++j;
            }
            if (found) {
                ++j;
                while (j < to && t.at(j).isLetter())
                    ++j;
                paint(i, j, m_purple);
                i = j;
                prev = '0';
            } else {
                ++i;
                prev = '/';
            }
        } else {
            prev = (c == ')' || c == ']') ? '0' : c;
            ++i;
        }
    }
}

void HtmlHighlighter::highlightCss(const QString &t, int from, int to, int &sub)
{
    bool comment = sub & 1;
    int depth = sub >> 1;
    QTextCharFormat commentFormat;
    commentFormat.setForeground(m_dim);
    commentFormat.setFontItalic(true);

    // colours the value part of a declaration
    auto value = [&](int start, int end) {
        int i = start;
        while (i < end) {
            const QChar c = t.at(i);
            if (c == '"' || c == '\'') {
                int j = t.indexOf(c, i + 1);
                j = (j < 0 || j >= end) ? end : j + 1;
                setFormat(i, j - i, fmt(m_value));
                i = j;
            } else if (c == '#' || c.isDigit() || ((c == '.' || c == '-') && i + 1 < end && t.at(i + 1).isDigit())) {
                int j = i + 1;
                while (j < end && (t.at(j).isLetterOrNumber() || t.at(j) == '.' || t.at(j) == '%'))
                    ++j;
                setFormat(i, j - i, fmt(m_entity));
                i = j;
            } else if (c == '!') {
                int j = i + 1;
                while (j < end && t.at(j).isLetter())
                    ++j;
                setFormat(i, j - i, fmt(m_doctype));
                i = j;
            } else if (c.isLetter() || c == '-') {
                int j = i + 1;
                while (j < end && (t.at(j).isLetterOrNumber() || t.at(j) == '-'))
                    ++j;
                setFormat(i, j - i, j < end && t.at(j) == '(' ? m_accent : m_value);
                i = j;
            } else {
                ++i;
            }
        }
    };

    int i = from;
    while (i < to) {
        if (comment) {
            const int end = t.indexOf("*/", i);
            if (end < 0 || end + 2 > to) {
                setFormat(i, to - i, commentFormat);
                i = to;
                break;
            }
            setFormat(i, end + 2 - i, commentFormat);
            i = end + 2;
            comment = false;
            continue;
        }
        const QChar c = t.at(i);
        if (c.isSpace()) {
            ++i;
        } else if (c == '/' && i + 1 < to && t.at(i + 1) == '*') {
            comment = true;
        } else if (c == '{' || c == '}' || c == ';') {
            setFormat(i, 1, fmt(m_dim));
            if (c == '{')
                depth = qMin(depth + 1, 60);
            else if (c == '}')
                depth = qMax(0, depth - 1);
            ++i;
        } else {
            // one segment: up to { } ; or a comment, skipping quoted text
            int j = i;
            while (j < to) {
                const QChar d = t.at(j);
                if (d == '"' || d == '\'') {
                    const int q = t.indexOf(d, j + 1);
                    j = (q < 0 || q >= to) ? to : q + 1;
                    continue;
                }
                if (d == '{' || d == '}' || d == ';' || (d == '/' && j + 1 < to && t.at(j + 1) == '*'))
                    break;
                ++j;
            }
            const QChar term = j < to ? t.at(j) : QChar();
            const QString segment = t.mid(i, j - i);
            if (c == '@') {
                int k = i + 1;
                while (k < j && (t.at(k).isLetterOrNumber() || t.at(k) == '-'))
                    ++k;
                setFormat(i, k - i, fmt(m_tag));
                value(k, j);
            } else if (term == '{' || (term.isNull() && (depth == 0 || !segment.contains(':')))) {
                setFormat(i, j - i, fmt(m_accent));
            } else {
                const int colon = segment.indexOf(':');
                if (colon < 0) {
                    setFormat(i, j - i, fmt(m_attr));
                } else {
                    setFormat(i, colon, fmt(m_attr));
                    setFormat(i + colon, 1, fmt(m_dim));
                    value(i + colon + 1, j);
                }
            }
            i = j;
        }
    }
    sub = (depth << 1) | (comment ? 1 : 0);
}

void HtmlHighlighter::highlightBlock(const QString &text)
{
    const int previous = qMax(0, previousBlockState());
    int state = previous % 10;
    int kind = (previous / 10) % 10;
    int sub = previous / 100;
    const int n = text.size();
    int i = 0;

    QTextCharFormat italicDim;
    italicDim.setForeground(m_dim);
    italicDim.setFontItalic(true);

    while (i < n) {
        if (state == Comment) {
            const int end = text.indexOf("-->", i);
            if (end < 0) {
                setFormat(i, n - i, italicDim);
                i = n;
            } else {
                setFormat(i, end + 3 - i, italicDim);
                i = end + 3;
                state = Text;
            }
        } else if (state == RawScript || state == RawStyle) {
            const QString closing = state == RawScript ? "</script" : "</style";
            const int end = text.indexOf(closing, i, Qt::CaseInsensitive);
            const int stop = end < 0 ? n : end;
            if (state == RawScript)
                highlightJs(text, i, stop, sub);
            else
                highlightCss(text, i, stop, sub);
            if (end < 0) {
                i = n;
            } else {
                i = end;
                state = Text;
                sub = 0;
            }
        } else if (state == InDouble || state == InSingle) {
            const QChar quote = state == InDouble ? '"' : '\'';
            const int end = text.indexOf(quote, i);
            if (end < 0) {
                setFormat(i, n - i, m_value);
                i = n;
            } else {
                setFormat(i, end + 1 - i, m_value);
                i = end + 1;
                state = InTag;
            }
        } else if (state == InTag) {
            const QChar ch = text.at(i);
            if (ch.isSpace()) {
                ++i;
            } else if (ch == '>') {
                setFormat(i, 1, m_dim);
                ++i;
                state = kind == Script ? RawScript : kind == Style ? RawStyle : Text;
                kind = Other;
                sub = 0;
            } else if (ch == '/' && i + 1 < n && text.at(i + 1) == '>') {
                setFormat(i, 2, m_dim);
                i += 2;
                state = Text;
                kind = Other;
            } else if (ch == '"' || ch == '\'') {
                state = ch == '"' ? InDouble : InSingle;
                setFormat(i, 1, m_value);
                ++i;
            } else if (ch == '=') {
                setFormat(i, 1, m_dim);
                ++i;
                while (i < n && text.at(i).isSpace())
                    ++i;
                if (i < n && text.at(i) != '"' && text.at(i) != '\'' && text.at(i) != '>') {
                    int j = i;
                    while (j < n && !text.at(j).isSpace() && text.at(j) != '>')
                        ++j;
                    setFormat(i, j - i, m_value);
                    i = j;
                }
            } else {
                int j = i;
                while (j < n && !text.at(j).isSpace() && text.at(j) != '=' && text.at(j) != '>' && text.at(j) != '"'
                       && text.at(j) != '\'' && !(text.at(j) == '/' && j + 1 < n && text.at(j + 1) == '>'))
                    ++j;
                if (j == i)
                    ++j;
                setFormat(i, j - i, m_attr);
                i = j;
            }
        } else { // Text
            const int lt = text.indexOf('<', i);
            if (lt < 0) {
                entities(text, i, n);
                i = n;
                continue;
            }
            entities(text, i, lt);
            i = lt;

            if (text.mid(i, 4) == "<!--") {
                const int end = text.indexOf("-->", i + 4);
                if (end < 0) {
                    setFormat(i, n - i, italicDim);
                    i = n;
                    state = Comment;
                } else {
                    setFormat(i, end + 3 - i, italicDim);
                    i = end + 3;
                }
            } else if (i + 1 < n && (text.at(i + 1) == '!' || text.at(i + 1) == '?')) {
                const int end = text.indexOf('>', i);
                const int stop = end < 0 ? n : end + 1;
                setFormat(i, stop - i, m_doctype);
                i = stop;
            } else if (i + 1 < n && (text.at(i + 1).isLetter() || text.at(i + 1) == '/')) {
                const bool closing = text.at(i + 1) == '/';
                const int nameStart = i + (closing ? 2 : 1);
                int j = nameStart;
                while (j < n && isNameChar(text.at(j)))
                    ++j;
                setFormat(i, nameStart - i, m_dim);
                setFormat(nameStart, j - nameStart, m_tag);
                const QString name = text.mid(nameStart, j - nameStart).toLower();
                kind = closing ? Other : name == "script" ? Script : name == "style" ? Style : Other;
                state = InTag;
                i = j;
            } else {
                ++i; // a lone "<"
            }
        }
    }

    int carried = state;
    if (state == InTag || state == InDouble || state == InSingle)
        carried += kind * 10;
    else if (state == RawScript || state == RawStyle)
        carried += sub * 100;
    setCurrentBlockState(carried);
}
