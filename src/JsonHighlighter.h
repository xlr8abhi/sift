#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

class JsonHighlighter : public QSyntaxHighlighter
{
public:
    explicit JsonHighlighter(QTextDocument *document);

    void setActive(bool active) { m_active = active; }
    void refreshColors();

protected:
    void highlightBlock(const QString &text) override;

private:
    bool m_active = false;
    QColor m_key;
    QColor m_string;
    QColor m_number;
    QColor m_keyword;
};
