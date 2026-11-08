#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QButtonGroup;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class IconButton;

// Body of the response "Headers" tab. Two views of the same headers:
//   Table: name and value in two columns, long values wrap, grouped (Content, Caching,
//          Security, Cookies, Other), every Set-Cookie split into name, value and attributes.
//   Raw:   the headers as the server sent them, one "name: value" per line (the existing
//          read-only viewer, so search, wrap and zoom work as in the Raw tab).
// Ctrl+click on a row of the table copies its value.
class HeadersPane : public QWidget
{
    Q_OBJECT

public:
    using Pair = QPair<QByteArray, QByteArray>;

    // `raw` is owned by the pane from now on
    explicit HeadersPane(QPlainTextEdit *raw, QWidget *parent = nullptr);

    void setHeaders(const QList<Pair> &pairs);
    bool rawActive() const;
    // Ctrl+F in the Table view goes to the filter box
    void focusFilter();
    // Name: value lines, as shown in Raw
    QString rawText() const;
    // Rebuilds the table after a theme change
    void refresh();

    // "content-type" -> "Content-Type"
    static QString properName(const QString &name);

private:
    struct Entry
    {
        QString name;
        QString value;
        int group = 0;
    };

    void rebuild();
    void setMode(bool raw);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void copyRow(const QPoint &viewportPos);

    QPlainTextEdit *m_raw;
    QTextBrowser *m_table;
    QStackedWidget *m_stack;
    QLineEdit *m_filter;
    QToolButton *m_tableBtn;
    QToolButton *m_rawBtn;
    QToolButton *m_groupBtn;
    IconButton *m_copyBtn;
    QList<Entry> m_entries;
    QList<int> m_shown; // entry index of each table row (-1 for a group strip), in display order
    bool m_grouped = true;
};
