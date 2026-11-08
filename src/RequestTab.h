#pragma once

#include "BuiltRequest.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QTextCursor>
#include <QSet>
#include <QUrl>
#include <QWidget>

class FoldingEdit;
class IconButton;
class PathBar;
class HeaderHighlighter;
class HeadersPane;
class HtmlHighlighter;
class JsonHighlighter;
class KeyValueTable;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QTextBrowser;
class RequestPreview;

class RequestTab : public QWidget
{
    Q_OBJECT

public:
    explicit RequestTab(QWidget *parent = nullptr);
    ~RequestTab() override;

    QJsonObject requestJson() const;
    void loadRequest(const QJsonObject &request);
    QString method() const;
    QString url() const;
    // True for a tab nobody has filled in: no URL, params, headers or body
    bool isBlank() const;

    void send();
    void cancel();
    void focusUrl();
    void openSearch();
    void showResponseView(int index);
    void showRequestEditor(int index);
    void setWrap(bool on);
    void copyResponse();
    // Copies the JSON path of the line you are on (shown in the bar under Pretty)
    void copyPath();
    void setSideBySide(bool on);

signals:
    void completed(const QJsonObject &request, int status, const QString &reason, qint64 ms, qint64 size);
    void summaryChanged();
    // The wrap button was clicked; the window applies it to every tab
    void wrapToggled(bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class BadgeKind { Idle, Pending, Ok, Warn, Err };

    void onFinished(QNetworkReply *reply);
    BuiltRequest buildRequest() const;
    void setIdleStatus();
    void setSending(bool sending);
    void setHighlighting(bool json, bool markup);
    void setBadge(const QString &text, BadgeKind kind);
    void updatePathBar();
    void onThemeChanged();
    void updateMethodColors();
    void refreshRequestCounts();
    void clearMeta();
    void updateMeta();
    void refreshPreview(bool focus);
    void fetchRenderImage(const QUrl &url);
    void abortRenderImages();
    void loadRenderStyles(const QString &html);
    void onUrlEdited(const QString &text);
    void onParamsChanged();
    void applySplitSizes();

    QPlainTextEdit *currentViewer() const;
    void closeSearch();
    void updateSearch(bool jump);
    void paintMatches(bool jump);
    void stepMatch(int delta);

    static QString formatSize(qint64 bytes);

    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QElapsedTimer m_timer;
    QJsonObject m_pending;

    QComboBox *m_method;
    QLineEdit *m_url;
    QPushButton *m_send;
    RequestPreview *m_previewPane;
    QSplitter *m_splitter;

    QTabWidget *m_reqTabs = nullptr;
    KeyValueTable *m_params;
    KeyValueTable *m_reqHeaders;
    QComboBox *m_bodyType;
    QStackedWidget *m_bodyStack;
    QStackedWidget *m_responseStack;
    bool m_cancelled = false;
    QPlainTextEdit *m_bodyText;
    KeyValueTable *m_form;
    bool m_syncing = false;

    QWidget *m_statusBar;
    QLabel *m_badge;
    QString m_badgeText;
    BadgeKind m_badgeKind = BadgeKind::Idle;
    QLabel *m_meta;
    IconButton *m_searchBtn;
    IconButton *m_wrapBtn;
    IconButton *m_copyBtn;
    PathBar *m_pathBar;
    QTimer *m_ageTimer;
    QString m_metaSize;
    qint64 m_metaMs = -1;
    QDateTime m_receivedAt;
    QTabWidget *m_tabs;
    FoldingEdit *m_pretty;
    JsonHighlighter *m_jsonHl;
    HtmlHighlighter *m_htmlPretty;
    HeaderHighlighter *m_headersHl;
    JsonHighlighter *m_bodyHl;
    QTextBrowser *m_render;
    QUrl m_renderBase;
    int m_renderGeneration = 0;
    QSet<QUrl> m_renderRequested;
    QList<QNetworkReply *> m_renderImages;
    QPlainTextEdit *m_raw;
    QPlainTextEdit *m_headers; // the Raw view inside m_headersPane
    HeadersPane *m_headersPane;

    QWidget *m_searchBar;
    QLineEdit *m_query;
    QLabel *m_matchCount;
    QList<QTextCursor> m_matches;
    int m_current = 0;

    bool m_wrap = false;
    bool m_sideBySide = false;
    bool m_resetSizes = false;
};
