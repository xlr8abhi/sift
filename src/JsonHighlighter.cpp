#include "JsonHighlighter.h"

#include "Theme.h"

#include <QTextCharFormat>

JsonHighlighter::JsonHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    const SemanticColors c = ThemeManager::instance().colors();
    m_key = c.key;
    m_string = c.string;
    m_number = c.number;
    m_keyword = c.keyword;
}

void JsonHighlighter::refreshColors()
{
    const SemanticColors c = ThemeManager::instance().colors();
    m_key = c.key;
    m_string = c.string;
    m_number = c.number;
    m_keyword = c.keyword;
    if (m_active)
        rehighlight();
}

void JsonHighlighter::highlightBlock(const QString &text)
{
    if (!m_active)
        return;

    const int n = text.size();
    int i = 0;
    while (i < n) {
        const QChar ch = text.at(i);

        if (ch == '"') {
            int j = i + 1;
            while (j < n) {
                if (text.at(j) == '\\')
                    j += 2;
                else if (text.at(j) == '"')
                    break;
                else
                    ++j;
            }
            const int end = qMin(j + 1, n);
            int k = end;
            while (k < n && text.at(k).isSpace())
                ++k;
            const bool isKey = k < n && text.at(k) == ':';
            setFormat(i, end - i, isKey ? m_key : m_string);
            i = end;
            continue;
        }

        if (ch.isDigit() || (ch == '-' && i + 1 < n && text.at(i + 1).isDigit())) {
            int j = i + 1;
            while (j < n && (text.at(j).isDigit() || text.at(j) == '.' || text.at(j) == 'e'
                             || text.at(j) == 'E' || text.at(j) == '+' || text.at(j) == '-'))
                ++j;
            setFormat(i, j - i, m_number);
            i = j;
            continue;
        }

        if (ch.isLetter()) {
            int j = i + 1;
            while (j < n && text.at(j).isLetter())
                ++j;
            const QStringView word = QStringView(text).mid(i, j - i);
            if (word == u"true" || word == u"false" || word == u"null")
                setFormat(i, j - i, m_keyword);
            i = j;
            continue;
        }

        ++i;
    }
}
