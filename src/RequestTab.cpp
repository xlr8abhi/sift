#include "AppFont.h"
#include "RequestTab.h"

#include "CurlParser.h"
#include "FoldingEdit.h"
#include "HeadersPane.h"
#include "HtmlHighlighter.h"
#include <QSyntaxHighlighter>
#include <QTextBrowser>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSharedPointer>
#include <QTextDocument>
#include <QImage>
#include <functional>
#include <QDesktopServices>
#include "JsonFormat.h"
#include "JsonHighlighter.h"
#include "KeyValueTable.h"
#include "RequestPreview.h"
#include "ResponseWidgets.h"
#include "Theme.h"

#include <QClipboard>
#include <QComboBox>
#include <QToolTip>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QPlainTextEdit>
#include <QApplication>
#include <QStyle>
#include <QFrame>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QWheelEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QDateTime>
#include <QTabBar>
#include <QTimer>
#include <QTabWidget>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

constexpr int kMaxMatches = 5000;
constexpr qint64 kMaxHighlightBytes = 2 * 1024 * 1024;

const QStringList kBodyTypes = {"none", "json", "form", "raw"};

QString statusPhrase(int code)
{
    switch (code) {
    case 200: return QStringLiteral("OK");
    case 201: return QStringLiteral("Created");
    case 202: return QStringLiteral("Accepted");
    case 204: return QStringLiteral("No Content");
    case 206: return QStringLiteral("Partial Content");
    case 301: return QStringLiteral("Moved Permanently");
    case 302: return QStringLiteral("Found");
    case 303: return QStringLiteral("See Other");
    case 304: return QStringLiteral("Not Modified");
    case 307: return QStringLiteral("Temporary Redirect");
    case 308: return QStringLiteral("Permanent Redirect");
    case 400: return QStringLiteral("Bad Request");
    case 401: return QStringLiteral("Unauthorized");
    case 403: return QStringLiteral("Forbidden");
    case 404: return QStringLiteral("Not Found");
    case 405: return QStringLiteral("Method Not Allowed");
    case 408: return QStringLiteral("Request Timeout");
    case 409: return QStringLiteral("Conflict");
    case 410: return QStringLiteral("Gone");
    case 415: return QStringLiteral("Unsupported Media Type");
    case 422: return QStringLiteral("Unprocessable Content");
    case 429: return QStringLiteral("Too Many Requests");
    case 500: return QStringLiteral("Internal Server Error");
    case 501: return QStringLiteral("Not Implemented");
    case 502: return QStringLiteral("Bad Gateway");
    case 503: return QStringLiteral("Service Unavailable");
    case 504: return QStringLiteral("Gateway Timeout");
    default: return {};
    }
}

}

// Qt's CSS parser only understands a CSS2-ish subset. Drop at-rules (media
// queries, font-face, keyframes, imports) rather than let them confuse it.
QString simplifyCss(QString css)
{
    static const QRegularExpression comment(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption);
    css.remove(comment);
    QString out;
    const int n = css.size();
    int i = 0;
    while (i < n) {
        if (css.at(i) != '@') {
            out += css.at(i++);
            continue;
        }
        const int semi = css.indexOf(';', i);
        const int open = css.indexOf('{', i);
        if (open < 0 || (semi >= 0 && semi < open)) {
            i = semi < 0 ? n : semi + 1; // @import / @charset / @namespace
            continue;
        }
        int depth = 0;
        int j = open;
        for (; j < n; ++j) {
            if (css.at(j) == '{')
                ++depth;
            else if (css.at(j) == '}' && --depth == 0)
                break;
        }
        i = j + 1;
    }
    return out;
}

// Tab bar whose tabs can carry a count ("Headers  4"). The caption is
// drawn here in one piece, so the tab is exactly as wide as its text.
class CountTabBar : public QTabBar
{
public:
    void setCount(int tab, int count, const QString &name)
    {
        if (tab < 0 || tab >= kSlots)
            return;
        m_counts[tab] = count;
        m_dots[tab] = false;
        m_names[tab] = name;
        setTabText(tab, count > 0 ? QString() : name);
        updateGeometry();
        update();
    }

    // A dot after the name instead of a number (the tab has content but nothing to count)
    void setDot(int tab, bool on, const QString &name)
    {
        if (tab < 0 || tab >= kSlots)
            return;
        m_counts[tab] = 0;
        m_dots[tab] = on;
        m_names[tab] = name;
        setTabText(tab, on ? QString() : name);
        updateGeometry();
        update();
    }

protected:
    QSize tabSizeHint(int index) const override
    {
        QSize size = QTabBar::tabSizeHint(index);
        if (index >= 0 && index < kSlots && marked(index))
            size.rwidth() += fontMetrics().horizontalAdvance(caption(index));
        return size;
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTabBar::paintEvent(event);
        QPainter p(this);
        p.setFont(font());
        for (int i = 0; i < kSlots && i < count(); ++i) {
            if (!marked(i))
                continue;
            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            const QRect box = style()->subElementRect(QStyle::SE_TabBarTabText, &opt, this);
            const bool lit = opt.state & (QStyle::State_Selected | QStyle::State_MouseOver);
            const QFontMetrics fm = p.fontMetrics();
            const QString name = m_names[i] + QStringLiteral("  ");
            const QString number = marker(i);
            const int width = fm.horizontalAdvance(name) + fm.horizontalAdvance(number);
            int x = box.center().x() - width / 2;
            const QRect line(x, box.top(), width, box.height());
            p.setPen(palette().color(lit ? QPalette::Text : QPalette::PlaceholderText));
            p.drawText(line, Qt::AlignVCenter | Qt::AlignLeft, name);
            x += fm.horizontalAdvance(name);
            p.setPen(ThemeManager::instance().colors().ok);
            p.drawText(QRect(x, box.top(), fm.horizontalAdvance(number), box.height()), Qt::AlignVCenter | Qt::AlignLeft, number);
        }
    }

private:
    bool marked(int i) const { return m_counts[i] > 0 || m_dots[i]; }
    QString marker(int i) const { return m_dots[i] ? QString(QChar(0x2022)) : QString::number(m_counts[i]); }
    QString caption(int i) const { return m_names[i] + QStringLiteral("  ") + marker(i); }

    static constexpr int kSlots = 4;
    int m_counts[kSlots] = {0, 0, 0, 0};
    bool m_dots[kSlots] = {false, false, false, false};
    QString m_names[kSlots];
};

class RequestTabWidget : public QTabWidget
{
public:
    RequestTabWidget() { setTabBar(new CountTabBar); }
    CountTabBar *countBar() const { return static_cast<CountTabBar *>(tabBar()); }
};

// Rotating arc on a round disc. The timer only runs while the widget is visible.
class Spinner : public QWidget
{
public:
    explicit Spinner(int size = 64)
    {
        setFixedSize(size, size);
        m_timer.setInterval(35);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 18) % 360;
            update();
        });
    }

protected:
    void showEvent(QShowEvent *) override { m_timer.start(); }
    void hideEvent(QHideEvent *) override { m_timer.stop(); }

    void paintEvent(QPaintEvent *) override
    {
        const QColor text = palette().color(QPalette::Text);
        const QColor base = palette().color(QPalette::Base);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor::fromRgbF(base.redF() * 0.9 + text.redF() * 0.1, base.greenF() * 0.9 + text.greenF() * 0.1,
                                    base.blueF() * 0.9 + text.blueF() * 0.1));
        p.drawEllipse(rect());

        const qreal inset = width() * 0.28;
        const qreal pen = qMax(2.0, width() * 0.045);
        const QRectF ring = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        QColor track = text;
        track.setAlphaF(0.12);
        p.setPen(QPen(track, pen));
        p.drawEllipse(ring);
        QColor arc = text;
        arc.setAlphaF(0.8);
        p.setPen(QPen(arc, pen, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, -m_angle * 16, -110 * 16);
    }

private:
    QTimer m_timer;
    int m_angle = 0;
};

// Shown in the response pane while a request is in flight.
class PendingState : public QWidget
{
public:
    PendingState()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(10);
        layout->addWidget(new Spinner(64), 0, Qt::AlignHCenter);
        auto *title = new QLabel(tr("Request in progress…"));
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);
        cancel = new QPushButton(tr("Cancel  Esc"));
        cancel->setCursor(Qt::PointingHandCursor);
        layout->addWidget(cancel, 0, Qt::AlignHCenter);
    }

    QPushButton *cancel;
};

// Shown in the response pane until a request has been sent.
class EmptyState : public QWidget
{
public:
    explicit EmptyState(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(10);
        auto *icon = new Icon;
        layout->addWidget(icon, 0, Qt::AlignHCenter);
        auto *title = new QLabel(tr("Not sent"));
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);
        auto *hint = new QLabel(tr("Press Ctrl+Enter to send the request"));
        hint->setObjectName("meta");
        hint->setAlignment(Qt::AlignCenter);
        layout->addWidget(hint);
    }

private:
    class Icon : public QWidget
    {
    public:
        Icon() { setFixedSize(64, 64); }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            const QColor text = palette().color(QPalette::Text);
            const QColor base = palette().color(QPalette::Base);
            QColor disc = QColor::fromRgbF(base.redF() * 0.9 + text.redF() * 0.1, base.greenF() * 0.9 + text.greenF() * 0.1,
                                           base.blueF() * 0.9 + text.blueF() * 0.1);
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(disc);
            p.drawEllipse(rect());

            QColor ink = palette().color(QPalette::PlaceholderText);
            p.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.translate(32, 32);
            QPainterPath page;
            page.moveTo(-8, 12);
            page.lineTo(-8, -10);
            page.quadTo(-8, -13, -5, -13);
            page.lineTo(4, -13);
            page.lineTo(10, -7);
            page.lineTo(10, 3);
            p.drawPath(page);
            p.drawLine(QPointF(-8, 12), QPointF(0, 12));
            p.drawLine(QPointF(-4, -6), QPointF(3, -6));
            p.drawLine(QPointF(-4, -2), QPointF(5, -2));
            p.drawLine(QPointF(-4, 2), QPointF(2, 2));
            p.drawLine(QPointF(-4, 6), QPointF(-1, 6));
            p.drawLine(QPointF(4, 8), QPointF(12, 8));
            p.drawLine(QPointF(9, 5), QPointF(12, 8));
            p.drawLine(QPointF(9, 11), QPointF(12, 8));
        }
    };
};

// Shown in the response pane after a request was cancelled.
class CancelledState : public QWidget
{
public:
    CancelledState()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(10);
        layout->addWidget(new Icon, 0, Qt::AlignHCenter);
        auto *title = new QLabel(tr("You cancelled this request"));
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);
        auto *hint = new QLabel(tr("Press Ctrl+Enter to send it again"));
        hint->setObjectName("meta");
        hint->setAlignment(Qt::AlignCenter);
        layout->addWidget(hint);
    }

private:
    class Icon : public QWidget
    {
    public:
        Icon() { setFixedSize(64, 64); }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            const QColor text = palette().color(QPalette::Text);
            const QColor base = palette().color(QPalette::Base);
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor::fromRgbF(base.redF() * 0.9 + text.redF() * 0.1, base.greenF() * 0.9 + text.greenF() * 0.1,
                                        base.blueF() * 0.9 + text.blueF() * 0.1));
            p.drawEllipse(rect());
            p.setPen(QPen(palette().color(QPalette::PlaceholderText), 1.9, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(23, 23), QPointF(41, 41));
            p.drawLine(QPointF(41, 23), QPointF(23, 41));
        }
    };
};

// Always drops straight down from the method box, whatever the widget style does.
// Combo whose list opens right below the box (not over it), used for the method and the body type.
class DropCombo : public QComboBox
{
public:
    using QComboBox::QComboBox;

    void showPopup() override
    {
        QComboBox::showPopup();
        if (QWidget *popup = view()->window())
            popup->move(mapToGlobal(QPoint(0, height() + 2)));
    }
};

// Combo lists paint their own rounded hover / current wash on top of the stylesheet. With verbs=true each
// item is a method name in its own colour, bold (the method list).
class MethodDelegate : public QStyledItemDelegate
{
public:
    explicit MethodDelegate(QObject *parent, bool verbs = true)
        : QStyledItemDelegate(parent)
        , m_verbs(verbs)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem o = option;
        initStyleOption(&o, index);
        if (m_verbs) {
            const QColor color = methodColor(index.data().toString());
            o.palette.setColor(QPalette::Text, color);
            o.palette.setColor(QPalette::HighlightedText, color);
            o.font.setBold(true);
        } else {
            o.palette.setColor(QPalette::HighlightedText, o.palette.color(QPalette::Text));
        }
        if (o.state & (QStyle::State_MouseOver | QStyle::State_Selected)) {
            QColor wash = o.palette.color(QPalette::Highlight);
            wash.setAlphaF(0.22);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(wash);
            painter->drawRoundedRect(QRectF(o.rect).adjusted(1, 1, -1, -1), 4, 4);
            painter->restore();
        }
        o.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected);
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &o, painter, o.widget);
    }

private:
    bool m_verbs;
};

// Renders HTML with Qt's own rich-text engine: no JavaScript and no stylesheets.
// Images are fetched in the background; the page shows without them first.
class RenderView : public QTextBrowser
{
public:
    std::function<void(const QUrl &)> fetchImage;

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource && fetchImage)
            fetchImage(name);
        return {};
    }
};

class HeaderHighlighter : public QSyntaxHighlighter
{
public:
    explicit HeaderHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        refreshColors();
    }

    void refreshColors()
    {
        m_c = ThemeManager::instance().colors();
        m_dim = QGuiApplication::palette().color(QPalette::PlaceholderText);
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override
    {
        const int colon = text.indexOf(':');
        if (colon <= 0)
            return;
        setFormat(0, colon, m_c.key);
        setFormat(colon, 1, m_dim);
        int start = colon + 1;
        while (start < text.size() && text.at(start) == ' ')
            ++start;
        const QString value = text.mid(start);
        bool numeric = !value.isEmpty();
        for (const QChar ch : value) {
            if (!ch.isDigit())
                numeric = false;
        }
        setFormat(start, value.size(), numeric ? m_c.number : m_c.string);
    }

private:
    SemanticColors m_c;
    QColor m_dim;
};

namespace {

void setupViewer(QPlainTextEdit *edit)
{
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setFont(AppFont::mono());
}

// Qt repaints only the rectangles it computes for the old and new highlights, and for matches inside a
// long wrapped line (minified JS) that leaves pieces of old highlights on screen. Repaint the viewport.
void setHighlights(QPlainTextEdit *edit, const QList<QPair<int, int>> &ranges, int current = -1)
{
    static_cast<FoldingEdit *>(edit)->setMatches(ranges, current);
}

QPlainTextEdit *makeViewer()
{
    // FoldingEdit with folding left off: same viewer, but it can scroll by pixel
    auto *edit = new FoldingEdit;
    setupViewer(edit);
    return edit;
}

QJsonArray rowsToJson(const QList<KeyValueTable::Row> &rows)
{
    QJsonArray out;
    for (const auto &row : rows)
        out.append(QJsonObject{{"key", row.key}, {"value", row.value}, {"on", row.enabled}});
    return out;
}

QList<KeyValueTable::Row> jsonToRows(const QJsonArray &array)
{
    QList<KeyValueTable::Row> out;
    for (const auto &value : array) {
        const QJsonObject obj = value.toObject();
        out.append({obj.value("key").toString(), obj.value("value").toString(),
                    obj.value("on").toBool(true)});
    }
    return out;
}

}

RequestTab::RequestTab(QWidget *parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_nam->setTransferTimeout(30000);

    m_method = new DropCombo;
    m_method->addItems({"GET", "POST", "PUT", "PATCH", "DELETE"});
    m_method->setFont(AppFont::mono());
    m_method->view()->setItemDelegate(new MethodDelegate(m_method));
    m_method->view()->setMouseTracking(true);
    m_method->setProperty("sift_bold", true); // always bold, whatever the weight setting

    m_url = new QLineEdit;
    m_url->setPlaceholderText("https://api.example.com/v1/items");
    m_url->setClearButtonEnabled(true);
    m_url->setFont(AppFont::mono());

    m_send = new QPushButton(tr("Send  Ctrl+Enter"));
    m_send->setDefault(true);

    m_send->setObjectName("sendButton");
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_method->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *urlBar = new QFrame;
    urlBar->setObjectName("urlBar");
    urlBar->setFixedHeight(38);
    auto *urlRow = new QHBoxLayout(urlBar);
    urlRow->setContentsMargins(0, 0, 0, 0);
    urlRow->setSpacing(0);
    urlRow->addWidget(m_method);
    urlRow->addWidget(m_url, 1);
    urlRow->addWidget(m_send);

    m_badge = new QLabel;
    m_meta = new QLabel;
    m_meta->setObjectName("meta");
    m_ageTimer = new QTimer(this);
    m_ageTimer->setInterval(20000);
    connect(m_ageTimer, &QTimer::timeout, this, &RequestTab::updateMeta);

    m_statusBar = new QWidget;
    auto *statusRow = new QHBoxLayout(m_statusBar);
    statusRow->setContentsMargins(10, 8, 10, 6);
    statusRow->addWidget(m_badge);
    statusRow->addSpacing(12);
    statusRow->addWidget(m_meta);
    statusRow->addStretch(1);

    m_searchBtn = new IconButton(IconButton::Kind::Search);
    m_searchBtn->setToolTip(tr("Search in response (Ctrl+F)"));
    m_wrapBtn = new IconButton(IconButton::Kind::Wrap);
    m_wrapBtn->setToolTip(tr("Wrap long lines (Alt+Z)"));
    m_copyBtn = new IconButton(IconButton::Kind::Copy);
    m_copyBtn->setToolTip(tr("Copy response (Ctrl+Shift+C)"));
    statusRow->setSpacing(2);
    statusRow->addWidget(m_searchBtn);
    statusRow->addWidget(m_wrapBtn);
    statusRow->addWidget(m_copyBtn);
    connect(m_searchBtn, &QAbstractButton::clicked, this, &RequestTab::openSearch);
    connect(m_wrapBtn, &QAbstractButton::clicked, this, [this](bool on) { emit wrapToggled(on); });
    connect(m_copyBtn, &QAbstractButton::clicked, this, &RequestTab::copyResponse);

    m_pretty = new FoldingEdit;
    setupViewer(m_pretty);
    m_jsonHl = new JsonHighlighter(m_pretty->document());
    m_htmlPretty = new HtmlHighlighter(m_pretty->document());
    m_raw = makeViewer();
    // one highlighter per document at a time: the HTML one stays detached until needed (Raw is never coloured)
    m_htmlPretty->setDocument(nullptr);
    m_raw->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_headers = makeViewer();
    m_headersHl = new HeaderHighlighter(m_headers->document());
    for (QPlainTextEdit *viewer : {static_cast<QPlainTextEdit *>(m_pretty), m_raw, m_headers}) {
        viewer->viewport()->installEventFilter(this);
        new KineticScroll(viewer);
    }
    m_headersPane = new HeadersPane(m_headers);

    m_tabs = new RequestTabWidget;
    m_tabs->setObjectName("respTabs");
    m_tabs->addTab(m_pretty, tr("Pretty"));
    m_tabs->addTab(m_raw, tr("Raw"));
    m_tabs->addTab(m_headersPane, tr("Headers"));

    auto *renderView = new RenderView;
    renderView->fetchImage = [this](const QUrl &url) { fetchRenderImage(url); };
    m_render = renderView;
    m_render->setObjectName("render");
    m_render->setOpenLinks(false);
    {
        // pages assume a white canvas, like a browser
        QPalette pal = m_render->palette();
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::Text, Qt::black);
        pal.setColor(QPalette::Link, QColor(0x00, 0x4a, 0xc8));
        m_render->setPalette(pal);
    }
    connect(m_render, &QTextBrowser::anchorClicked, this, [this](const QUrl &link) {
        const QUrl target = m_renderBase.resolved(link);
        if (target.scheme() == "http" || target.scheme() == "https")
            QDesktopServices::openUrl(target);
    });
    m_tabs->addTab(m_render, tr("Render"));
    m_render->viewport()->installEventFilter(this);
    new KineticScroll(m_render);
    m_tabs->setTabVisible(3, false);

    m_searchBar = new QWidget;
    m_query = new QLineEdit;
    m_query->setPlaceholderText(tr("Search in response…"));
    m_query->setClearButtonEnabled(true);
    m_query->installEventFilter(this);
    m_matchCount = new QLabel;
    auto *prev = new QToolButton;
    prev->setText("↑");
    prev->setAutoRaise(true);
    prev->setToolTip(tr("Previous (Shift+Enter)"));
    auto *next = new QToolButton;
    next->setText("↓");
    next->setAutoRaise(true);
    next->setToolTip(tr("Next (Enter)"));
    auto *close = new QToolButton;
    close->setText("✕");
    close->setAutoRaise(true);
    close->setToolTip(tr("Close (Esc)"));

    auto *searchLayout = new QHBoxLayout(m_searchBar);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->addWidget(m_query, 1);
    searchLayout->addWidget(m_matchCount);
    searchLayout->addWidget(prev);
    searchLayout->addWidget(next);
    searchLayout->addWidget(close);
    m_searchBar->hide();

    m_params = new KeyValueTable;
    m_reqHeaders = new KeyValueTable;
    m_form = new KeyValueTable;

    m_bodyText = new QPlainTextEdit;
    m_bodyText->setFont(AppFont::mono());
    m_bodyText->setPlaceholderText(tr("Request body"));
    auto *noBody = new QLabel(tr("This request has no body."));
    noBody->setAlignment(Qt::AlignCenter);

    m_bodyStack = new QStackedWidget;
    m_bodyStack->addWidget(noBody);
    m_bodyStack->addWidget(m_bodyText);
    m_bodyStack->addWidget(m_form);

    m_bodyHl = new JsonHighlighter(m_bodyText->document());
    m_bodyType = new DropCombo;
    m_bodyType->addItems({tr("None"), "JSON", tr("Form"), tr("Raw")});
    m_bodyType->view()->setItemDelegate(new MethodDelegate(m_bodyType, false));
    m_bodyType->setSizeAdjustPolicy(QComboBox::AdjustToContents); // as wide as its widest item, popup included

    auto *bodyPage = new QWidget;
    auto *bodyLayout = new QVBoxLayout(bodyPage);
    auto *bodyTypeRow = new QHBoxLayout;
    bodyTypeRow->addWidget(new QLabel(tr("Type")));
    bodyTypeRow->addWidget(m_bodyType);
    bodyTypeRow->addStretch(1);
    bodyLayout->addLayout(bodyTypeRow);
    bodyLayout->addWidget(m_bodyStack, 1);

    m_reqTabs = new RequestTabWidget;
    m_reqTabs->setObjectName("reqTabs");
    // Preview comes first, but a new request starts on Params
    m_previewPane = new RequestPreview;
    m_reqTabs->addTab(m_previewPane, tr("Preview"));
    m_reqTabs->addTab(m_params, tr("Params"));
    m_reqTabs->addTab(m_reqHeaders, tr("Headers"));
    m_reqTabs->addTab(bodyPage, tr("Body"));
    m_reqTabs->setCurrentIndex(1);

    auto *responsePane = new QWidget;
    auto *responseLayout = new QVBoxLayout(responsePane);
    responseLayout->setContentsMargins(0, 0, 0, 0);
    responseLayout->addWidget(m_statusBar);
    responseLayout->addWidget(m_searchBar);
    m_responseStack = new QStackedWidget;
    m_responseStack->addWidget(new EmptyState);
    m_responseStack->addWidget(m_tabs);
    auto *pending = new PendingState;
    connect(pending->cancel, &QPushButton::clicked, this, &RequestTab::cancel);
    m_responseStack->addWidget(pending);
    m_responseStack->addWidget(new CancelledState);
    responseLayout->addWidget(m_responseStack, 1);

    m_pathBar = new PathBar;
    m_pathBar->hide();
    responseLayout->addWidget(m_pathBar);
    m_pretty->setPathCallback([this](const QString &path) { m_pathBar->setPath(path); });
    m_pretty->setCopyPathCallback([this](const QString &path) {
        QApplication::clipboard()->setText(path);
        m_pathBar->flashCopied();
    });
    m_pretty->setScrollingCallback([this](bool scrolling) { m_pathBar->setBusy(scrolling); });

    m_splitter = new QSplitter(Qt::Vertical);
    m_splitter->addWidget(m_reqTabs);
    m_splitter->addWidget(responsePane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setChildrenCollapsible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(10);
    layout->addWidget(urlBar);
    layout->addWidget(m_splitter, 1);

    connect(m_url, &QLineEdit::textEdited, this, &RequestTab::onUrlEdited);
    connect(m_url, &QLineEdit::textChanged, this, &RequestTab::summaryChanged);
    connect(m_method, &QComboBox::currentIndexChanged, this, &RequestTab::summaryChanged);
    connect(m_params, &KeyValueTable::changed, this, &RequestTab::onParamsChanged);
    connect(m_reqHeaders, &KeyValueTable::changed, this, &RequestTab::refreshRequestCounts);
    connect(m_bodyType, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_bodyHl->setActive(index == 1);
        m_bodyHl->rehighlight();
        m_bodyStack->setCurrentIndex(index == 0 ? 0 : index == 2 ? 2 : 1);
        refreshRequestCounts();
    });
    connect(m_bodyText, &QPlainTextEdit::textChanged, this, &RequestTab::refreshRequestCounts);
    connect(m_form, &KeyValueTable::changed, this, &RequestTab::refreshRequestCounts);
    connect(m_method, &QComboBox::currentIndexChanged, this, &RequestTab::updateMethodColors);

    connect(m_query, &QLineEdit::textChanged, this, [this] {
        m_current = 0;
        updateSearch(true);
    });
    connect(prev, &QToolButton::clicked, this, [this] { stepMatch(-1); });
    connect(next, &QToolButton::clicked, this, [this] { stepMatch(1); });
    connect(close, &QToolButton::clicked, this, &RequestTab::closeSearch);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        updateSearch(false);
        updatePathBar();
    });

    connect(m_send, &QPushButton::clicked, this, [this] {
        if (m_reply)
            cancel();
        else
            send();
    });
    connect(m_reqTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0)
            refreshPreview(true);
    });
    connect(m_url, &QLineEdit::textChanged, this, [this] { refreshPreview(false); });
    connect(m_method, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(false); });
    connect(m_url, &QLineEdit::returnPressed, this, &RequestTab::send);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &RequestTab::onThemeChanged);

    for (const char *keys : {"Ctrl+Return", "Ctrl+Enter"})
        connect(new QShortcut(QKeySequence(keys), this), &QShortcut::activated, this, &RequestTab::send);
    connect(new QShortcut(QKeySequence("Ctrl+L"), this), &QShortcut::activated, this, &RequestTab::focusUrl);
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, [this] {
        if (m_searchBar->isVisible())
            closeSearch();
        else if (m_reply)
            cancel();
    });
    for (int i = 0; i < 4; ++i) {
        connect(new QShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)), this),
                &QShortcut::activated, this, [this, i] { showRequestEditor(i); });
    }

    refreshRequestCounts();
    updateMethodColors();
    setIdleStatus();
}

RequestTab::~RequestTab()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

QString RequestTab::method() const
{
    return m_method->currentText();
}

QString RequestTab::url() const
{
    return m_url->text().trimmed();
}

void RequestTab::focusUrl()
{
    m_url->setFocus();
    m_url->selectAll();
}

void RequestTab::abortRenderImages()
{
    ++m_renderGeneration;
    m_renderRequested.clear();
    for (QNetworkReply *image : std::as_const(m_renderImages)) {
        image->disconnect(this);
        image->abort();
        image->deleteLater();
    }
    m_renderImages.clear();
}

void RequestTab::loadRenderStyles(const QString &html)
{
    constexpr int kMaxSheets = 12;
    constexpr qint64 kMaxSheetBytes = 2 * 1024 * 1024;
    static const QRegularExpression linkTag(R"re(<link\b[^>]*>)re", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression relAttr(R"re(\brel\s*=\s*["']?[^"'>]*stylesheet)re", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mediaAttr(R"re(\bmedia\s*=\s*["']?print)re", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefAttr(R"re(\bhref\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))re", QRegularExpression::CaseInsensitiveOption);

    QList<QUrl> sheets;
    for (auto it = linkTag.globalMatch(html); it.hasNext() && sheets.size() < kMaxSheets;) {
        const QString tag = it.next().captured(0);
        if (!relAttr.match(tag).hasMatch() || mediaAttr.match(tag).hasMatch())
            continue;
        const QRegularExpressionMatch href = hrefAttr.match(tag);
        if (!href.hasMatch())
            continue;
        const QString value = href.captured(1) + href.captured(2) + href.captured(3);
        const QUrl url = m_renderBase.resolved(QUrl(value.trimmed()));
        if ((url.scheme() == "http" || url.scheme() == "https") && !sheets.contains(url))
            sheets << url;
    }
    if (sheets.isEmpty())
        return;

    struct Pending
    {
        QStringList css;
        int left;
    };
    auto state = QSharedPointer<Pending>::create();
    state->css = QStringList(sheets.size());
    state->left = sheets.size();
    const int generation = m_renderGeneration;

    for (int index = 0; index < sheets.size(); ++index) {
        QNetworkRequest request(sheets.at(index));
        request.setRawHeader("Accept", "text/css,*/*;q=0.1");
        request.setRawHeader("Referer", m_renderBase.toEncoded());
        QNetworkReply *sheet = m_nam->get(request);
        m_renderImages.append(sheet);
        connect(sheet, &QNetworkReply::finished, this, [this, sheet, state, index, generation, html] {
            m_renderImages.removeOne(sheet);
            sheet->deleteLater();
            if (generation != m_renderGeneration)
                return;
            if (sheet->error() == QNetworkReply::NoError)
                state->css[index] = simplifyCss(QString::fromUtf8(sheet->read(kMaxSheetBytes)));
            if (--state->left > 0)
                return;

            const QString styles = QStringLiteral("<style type=\"text/css\">") + state->css.join('\n') + QStringLiteral("</style>");
            QString styled = html;
            const int head = styled.indexOf("</head>", 0, Qt::CaseInsensitive);
            styled.insert(head >= 0 ? head : 0, styles);

            const int scroll = m_render->verticalScrollBar()->value();
            abortRenderImages(); // the document is rebuilt, so images are requested again
            m_render->document()->setBaseUrl(m_renderBase);
            m_render->setHtml(styled);
            m_render->verticalScrollBar()->setValue(scroll);
        });
    }
}

void RequestTab::fetchRenderImage(const QUrl &url)
{
    constexpr int kMaxImages = 150;
    constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;
    if ((url.scheme() != "http" && url.scheme() != "https") || m_renderRequested.contains(url)
        || m_renderRequested.size() >= kMaxImages)
        return;
    m_renderRequested.insert(url);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "image/*,*/*;q=0.8");
    request.setRawHeader("Referer", m_renderBase.toEncoded());
    QNetworkReply *image = m_nam->get(request);
    m_renderImages.append(image);
    connect(image, &QNetworkReply::finished, this, [this, image, url] {
        m_renderImages.removeOne(image);
        image->deleteLater();
        if (image->error() != QNetworkReply::NoError)
            return;
        const QByteArray data = image->read(kMaxImageBytes);
        QImage picture;
        if (!picture.loadFromData(data))
            return;
        QTextDocument *doc = m_render->document();
        doc->addResource(QTextDocument::ImageResource, url, picture);
        doc->markContentsDirty(0, doc->characterCount());
        m_render->viewport()->update();
    });
}

void RequestTab::showResponseView(int index)
{
    if (!m_tabs->isTabVisible(index))
        return;
    m_tabs->setCurrentIndex(index);
}

void RequestTab::showRequestEditor(int index)
{
    m_reqTabs->setCurrentIndex(index);
}

void RequestTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_resetSizes) {
        m_resetSizes = false;
        applySplitSizes();
    }
}

void RequestTab::setWrap(bool on)
{
    m_wrap = on;
    const QSignalBlocker blocker(m_wrapBtn);
    m_wrapBtn->setChecked(on);
    const auto mode = on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap;
    m_pretty->setLineWrapMode(mode);
    m_headers->setLineWrapMode(mode);
    m_raw->setLineWrapMode(QPlainTextEdit::WidgetWidth);
}

void RequestTab::applySplitSizes()
{
    const int total = m_sideBySide ? m_splitter->width() : m_splitter->height();
    if (total <= 100)
        return;
    if (m_sideBySide)
        m_splitter->setSizes({total * 2 / 5, total * 3 / 5});
    else
        m_splitter->setSizes({total * 3 / 10, total * 7 / 10});
}

void RequestTab::setSideBySide(bool on)
{
    m_sideBySide = on;
    m_splitter->setOrientation(on ? Qt::Horizontal : Qt::Vertical);
    m_splitter->setStretchFactor(0, on ? 2 : 1);
    m_splitter->setStretchFactor(1, 3);

    if (isVisible())
        applySplitSizes();
    else
        m_resetSizes = true;
}

void RequestTab::setBadge(const QString &text, BadgeKind kind)
{
    m_badgeText = text;
    m_badgeKind = kind;
    m_badge->setText(text);
    m_statusBar->setVisible(kind != BadgeKind::Idle && kind != BadgeKind::Pending);
    m_responseStack->setCurrentIndex(kind == BadgeKind::Idle ? 0 : kind == BadgeKind::Pending ? 2 : m_cancelled ? 3 : 1);
    updatePathBar();

    if (kind == BadgeKind::Idle) {
        m_badge->setStyleSheet("padding: 2px 9px;");
        return;
    }
    const SemanticColors colors = ThemeManager::instance().colors();
    QColor color = palette().color(QPalette::PlaceholderText);
    if (kind == BadgeKind::Ok)
        color = colors.ok;
    else if (kind == BadgeKind::Warn)
        color = colors.warn;
    else if (kind == BadgeKind::Err)
        color = colors.err;
    m_badge->setStyleSheet(QString("font-family: monospace; font-weight: bold; padding: 2px 9px;"
                                   "border-radius: 5px; background: rgba(%2,%3,%4,0.16); color: %1;")
                               .arg(color.name())
                               .arg(color.red())
                               .arg(color.green())
                               .arg(color.blue()));
}

void RequestTab::setIdleStatus()
{
    m_cancelled = false;
    setBadge(QString(), BadgeKind::Idle);
    clearMeta();
}

void RequestTab::onThemeChanged()
{
    m_reqTabs->tabBar()->update();
    setBadge(m_badgeText, m_badgeKind);
    updateMethodColors();
    m_jsonHl->refreshColors();
    m_bodyHl->refreshColors();
    m_htmlPretty->refreshColors();
    m_headersHl->refreshColors();
    m_headersPane->refresh();
}

void RequestTab::updateMethodColors()
{
    for (int i = 0; i < m_method->count(); ++i)
        m_method->setItemData(i, methodColor(m_method->itemText(i)), Qt::ForegroundRole);

    const QColor current = methodColor(m_method->currentText());
    QFont font = m_method->font();
    font.setBold(true);
    m_method->setFont(font);

    m_method->setStyleSheet(QString("QComboBox { color: %1; }").arg(current.name()));
}

void RequestTab::clearMeta()
{
    m_metaSize.clear();
    m_metaMs = -1;
    m_receivedAt = QDateTime();
    m_ageTimer->stop();
    m_meta->clear();
}

// "90.49 KB  334ms  2m ago": size, how long the server took, and how long ago it arrived.
void RequestTab::updateMeta()
{
    QStringList parts;
    if (!m_metaSize.isEmpty())
        parts << m_metaSize;
    if (m_metaMs >= 0)
        parts << tr("%1ms").arg(m_metaMs);
    if (m_receivedAt.isValid()) {
        const qint64 seconds = m_receivedAt.secsTo(QDateTime::currentDateTime());
        if (seconds < 60)
            parts << tr("now");
        else if (seconds < 3600)
            parts << tr("%1m ago").arg(seconds / 60);
        else if (seconds <= 3 * 3600) {
            const qint64 hours = seconds / 3600;
            const qint64 minutes = seconds % 3600 / 60;
            parts << (minutes == 0 ? tr("%1h ago").arg(hours) : tr("%1h %2m ago").arg(hours).arg(minutes));
        }
        else
            parts << tr("long ago");
    }
    m_meta->setText(parts.join("  "));
}

QString RequestTab::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

QPlainTextEdit *RequestTab::currentViewer() const
{
    if (m_tabs->currentWidget() == m_headersPane)
        return m_headersPane->rawActive() ? m_headers : nullptr; // the table has its own filter
    return qobject_cast<QPlainTextEdit *>(m_tabs->currentWidget());
}

bool RequestTab::eventFilter(QObject *watched, QEvent *event)
{
    // Ctrl+scroll zoom with a floor and a ceiling. Qt's own zoom has no lower
    // limit: trackpad steps shrink the font to ~0 pt, every line then fits on
    // screen and the app freezes laying out the whole response.
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        auto *edit = qobject_cast<QWidget *>(watched)->parentWidget();
        if (edit && (wheel->modifiers() & Qt::ControlModifier)) {
            constexpr qreal kMinPt = 6.0;
            constexpr qreal kMaxPt = 36.0;
            QFont font = edit->font();
            const qreal size = qBound(kMinPt, font.pointSizeF() + wheel->angleDelta().y() / 120.0, kMaxPt);
            if (!qFuzzyCompare(size, font.pointSizeF())) {
                font.setPointSizeF(size);
                edit->setFont(font);
            }
            return true;
        }
    }

    if (watched == m_query && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (key->modifiers() & Qt::ControlModifier)
                return false;
            stepMatch(key->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RequestTab::openSearch()
{
    if (m_tabs->currentWidget() == m_headersPane && !m_headersPane->rawActive()) {
        m_headersPane->focusFilter();
        return;
    }
    // Selected text in the viewer becomes the query. Line breaks and tabs are written as \n and \t (shown
    // in the box), which the search reads back as the real characters, like HTTPie.
    QString picked;
    if (auto *edit = currentViewer()) {
        picked = edit->textCursor().selectedText();
        picked.replace('\\', "\\\\");
        picked.replace(QChar(0x2029), "\\n");
        picked.replace('\t', "\\t");
        picked = picked.left(2000);
    }
    if (!picked.trimmed().isEmpty())
        m_query->setText(picked);
    m_searchBar->show();
    m_query->setFocus();
    m_query->selectAll();
    updateSearch(!picked.trimmed().isEmpty());
}

void RequestTab::closeSearch()
{
    m_searchBar->hide();
    m_matches.clear();
    for (QPlainTextEdit *edit : {static_cast<QPlainTextEdit *>(m_pretty), m_raw, m_headers})
        setHighlights(edit, {});
    if (auto *edit = currentViewer())
        edit->setFocus();
}

void RequestTab::updateSearch(bool jump)
{
    m_matches.clear();
    for (QPlainTextEdit *edit : {static_cast<QPlainTextEdit *>(m_pretty), m_raw, m_headers})
        setHighlights(edit, {});

    auto *edit = currentViewer();
    const QString query = m_query->text();
    if (!m_searchBar->isVisible() || !edit || query.isEmpty()) {
        m_matchCount->clear();
        return;
    }

    // "\n" and "\t" in the query mean a line break and a tab, so a selection of several lines can be searched
    // (QTextDocument::find cannot match across lines, so search the plain text; offsets are the same)
    QString needle;
    for (int i = 0; i < query.size(); ++i) {
        if (query.at(i) == '\\' && i + 1 < query.size() && QString("nt\\").contains(query.at(i + 1))) {
            const QChar next = query.at(++i);
            needle += next == 'n' ? QChar('\n') : next == 't' ? QChar('\t') : QChar('\\');
        } else {
            needle += query.at(i);
        }
    }
    const QString haystack = edit->toPlainText();
    QTextDocument *doc = edit->document();
    for (int at = haystack.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0 && m_matches.size() < kMaxMatches;
         at = haystack.indexOf(needle, at + qMax(1, needle.size()), Qt::CaseInsensitive)) {
        QTextCursor cursor(doc);
        cursor.setPosition(at);
        cursor.setPosition(at + needle.size(), QTextCursor::KeepAnchor);
        m_matches.append(cursor);
    }
    if (m_current >= m_matches.size())
        m_current = 0;
    paintMatches(jump);
}

void RequestTab::paintMatches(bool jump)
{
    auto *edit = currentViewer();
    if (!edit)
        return;

    if (m_matches.isEmpty()) {
        m_matchCount->setText(m_query->text().isEmpty() ? QString() : tr("No matches"));
        setHighlights(edit, {});
        return;
    }

    QList<QPair<int, int>> ranges;
    ranges.reserve(m_matches.size());
    for (const QTextCursor &match : m_matches)
        ranges.append({match.selectionStart(), match.selectionEnd()});
    setHighlights(edit, ranges, m_current);

    m_matchCount->setText(QString("%1 / %2%3")
                              .arg(m_current + 1)
                              .arg(m_matches.size())
                              .arg(m_matches.size() >= kMaxMatches ? "+" : ""));

    if (jump) {
        QTextCursor view = m_matches.at(m_current);
        view.clearSelection();
        if (edit == m_pretty)
            m_pretty->revealBlock(view.blockNumber());
        edit->setTextCursor(view);
        edit->centerCursor();
    }
}

void RequestTab::stepMatch(int delta)
{
    if (m_matches.isEmpty())
        return;
    const int n = m_matches.size();
    m_current = (m_current + delta + n) % n;
    paintMatches(true);
}

QJsonObject RequestTab::requestJson() const
{
    QJsonObject body;
    body["type"] = kBodyTypes.value(m_bodyType->currentIndex());
    body["text"] = m_bodyText->toPlainText();
    body["form"] = rowsToJson(m_form->rows());

    QJsonObject request;
    request["method"] = m_method->currentText();
    request["url"] = m_url->text().trimmed();
    request["params"] = rowsToJson(m_params->rows());
    request["headers"] = rowsToJson(m_reqHeaders->rows());
    request["body"] = body;
    return request;
}

bool RequestTab::isBlank() const
{
    return m_url->text().trimmed().isEmpty() && m_params->rows().isEmpty() && m_reqHeaders->rows().isEmpty()
        && m_bodyText->toPlainText().isEmpty() && m_form->rows().isEmpty();
}

void RequestTab::loadRequest(const QJsonObject &request)
{
    m_syncing = true;
    const int method = m_method->findText(request.value("method").toString());
    if (method >= 0)
        m_method->setCurrentIndex(method);
    m_url->setText(request.value("url").toString());
    m_params->setRows(jsonToRows(request.value("params").toArray()));
    m_reqHeaders->setRows(jsonToRows(request.value("headers").toArray()));

    const QJsonObject body = request.value("body").toObject();
    const int type = kBodyTypes.indexOf(body.value("type").toString());
    m_bodyType->setCurrentIndex(type < 0 ? 0 : type);
    QString bodyText = body.value("text").toString();
    if (type == 1)
        bodyText = prettyJson(bodyText);
    m_bodyText->setPlainText(bodyText);
    m_form->setRows(jsonToRows(body.value("form").toArray()));
    m_syncing = false;

    refreshRequestCounts();
}

void RequestTab::updatePathBar()
{
    m_pathBar->setVisible(m_responseStack->currentWidget() == m_tabs && m_tabs->currentWidget() == m_pretty
                          && m_pretty->foldingEnabled());
}

void RequestTab::copyResponse()
{
    QPlainTextEdit *edit = currentViewer();
    const QString text = m_tabs->currentWidget() == m_headersPane ? m_headersPane->rawText()
                         : edit                                   ? edit->toPlainText()
                                                                  : m_raw->toPlainText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
    QToolTip::showText(m_copyBtn->mapToGlobal(QPoint(0, m_copyBtn->height())), tr("Copied"), m_copyBtn, {}, 1200);
}

void RequestTab::copyPath()
{
    const QString path = m_pretty->currentPath();
    if (path.isEmpty())
        return;
    QApplication::clipboard()->setText(path);
    m_pathBar->flashCopied();
}

void RequestTab::refreshRequestCounts()
{
    // A count shows after the tab name; zero shows nothing.
    auto *bar = static_cast<RequestTabWidget *>(m_reqTabs)->countBar();
    bar->setCount(1, m_params->activeCount(), tr("Params"));
    bar->setCount(2, m_reqHeaders->activeCount(), tr("Headers"));
    // Body has nothing to count, so it only shows a dot when there is something to send
    const int type = m_bodyType->currentIndex();
    const bool hasBody = type == 2 ? m_form->activeCount() > 0
                         : type == 0 ? false
                                     : !m_bodyText->toPlainText().trimmed().isEmpty();
    bar->setDot(3, hasBody, tr("Body"));
}

void RequestTab::onUrlEdited(const QString &text)
{
    if (m_syncing)
        return;

    if (looksLikeCurl(text)) {
        const CurlImport imported = parseCurl(text);
        if (imported.ok) {
            loadRequest(imported.request);
            if (!imported.note.isEmpty())
                setBadge(tr("cURL import skipped: %1").arg(imported.note), BadgeKind::Warn);
            return;
        }
    }

    m_syncing = true;
    const QUrlQuery query(QUrl(text, QUrl::TolerantMode));
    KeyValueTable::Items items;
    for (const auto &pair : query.queryItems(QUrl::FullyDecoded))
        items.append(pair);
    m_params->setItems(items);
    m_syncing = false;
    refreshRequestCounts();
}

void RequestTab::onParamsChanged()
{
    refreshRequestCounts();
    if (m_syncing || m_url->text().trimmed().isEmpty())
        return;
    m_syncing = true;
    QUrl url(m_url->text().trimmed(), QUrl::TolerantMode);
    QUrlQuery query;
    for (const auto &pair : m_params->items())
        query.addQueryItem(pair.first, pair.second);
    url.setQuery(query);
    m_url->setText(url.toString());
    m_syncing = false;
}

BuiltRequest RequestTab::buildRequest() const
{
    BuiltRequest built;
    built.method = m_method->currentText();
    built.url = QUrl::fromUserInput(m_url->text().trimmed());

    bool userContentType = false;
    for (const auto &pair : m_reqHeaders->items()) {
        built.headers.append({pair.first, pair.second});
        if (pair.first.compare("Content-Type", Qt::CaseInsensitive) == 0)
            userContentType = true;
    }

    QString autoContentType;
    switch (m_bodyType->currentIndex()) {
    case 1:
        built.body = m_bodyText->toPlainText().toUtf8();
        autoContentType = "application/json";
        break;
    case 2: {
        QUrlQuery form;
        for (const auto &pair : m_form->items())
            form.addQueryItem(pair.first, pair.second);
        built.body = form.toString(QUrl::FullyEncoded).toUtf8();
        autoContentType = "application/x-www-form-urlencoded";
        break;
    }
    case 3:
        built.body = m_bodyText->toPlainText().toUtf8();
        break;
    default:
        break;
    }
    if (!autoContentType.isEmpty() && !userContentType)
        built.headers.append({"Content-Type", autoContentType});
    return built;
}

void RequestTab::refreshPreview(bool focus)
{
    if (m_reqTabs->currentIndex() != 0)
        return;
    if (m_url->text().trimmed().isEmpty())
        m_previewPane->clear();
    else
        m_previewPane->setRequest(buildRequest());
    if (focus)
        m_previewPane->focusView();
}

void RequestTab::send()
{
    const QString text = m_url->text().trimmed();
    if (text.isEmpty()) {
        m_url->setFocus();
        return;
    }

    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }

    const BuiltRequest built = buildRequest();
    QNetworkRequest request(built.url);
    for (const auto &pair : built.headers)
        request.setRawHeader(pair.first.toUtf8(), pair.second.toUtf8());

    m_timer.start();
    m_pending = requestJson();
    m_cancelled = false;
    setBadge(tr("Sending…"), BadgeKind::Pending);
    clearMeta();
    setSending(true);

    QNetworkReply *reply;
    if (built.method == "GET" && built.body.isEmpty())
        reply = m_nam->get(request);
    else
        reply = m_nam->sendCustomRequest(request, built.method.toUtf8(), built.body);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onFinished(reply); });
}

void RequestTab::setHighlighting(bool json, bool markup)
{
    m_jsonHl->setActive(json);
    m_jsonHl->setDocument(json ? m_pretty->document() : nullptr);
    m_htmlPretty->setDocument(markup ? m_pretty->document() : nullptr);
}

void RequestTab::setSending(bool sending)
{
    m_send->setText(sending ? tr("Cancel  Esc") : tr("Send  Ctrl+Enter"));
}

void RequestTab::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();

    setSending(false);
    clearMeta();
    m_metaMs = m_timer.elapsed();
    updateMeta();
    m_cancelled = true;
    setBadge(tr("Cancelled"), BadgeKind::Warn);
}

void RequestTab::onFinished(QNetworkReply *reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;
    m_cancelled = false;

    const qint64 ms = m_timer.elapsed();
    setSending(false);

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QByteArray body = reply->readAll();

    m_headersPane->setHeaders(reply->rawHeaderPairs());
    static_cast<RequestTabWidget *>(m_tabs)->countBar()->setCount(2, reply->rawHeaderPairs().size(), tr("Headers"));

    m_metaSize = formatSize(body.size());
    m_metaMs = ms;
    m_receivedAt = QDateTime::currentDateTime();
    m_ageTimer->start();
    updateMeta();

    if (!statusAttr.isValid()) {
        setBadge(tr("Error"), BadgeKind::Err);
        const QString message = reply->errorString();
        setHighlighting(false, false);
        abortRenderImages();
        m_render->clear();
        m_tabs->setTabVisible(3, false);
        m_pretty->setPlainText(message);
        m_pretty->resetFolds(false);
        m_raw->setPlainText(message);
        updatePathBar();
        updateSearch(false);
        emit completed(m_pending, 0, message, ms, body.size());
        reply->deleteLater();
        return;
    }

    const int code = statusAttr.toInt();
    QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    if (reason.isEmpty())
        reason = statusPhrase(code); // HTTP/2 sends no reason phrase
    const BadgeKind kind = code >= 400 ? BadgeKind::Err : code >= 300 ? BadgeKind::Warn : BadgeKind::Ok;
    setBadge(QString("%1 %2").arg(code).arg(reason).trimmed(), kind);

    const QString rawText = QString::fromUtf8(body);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    const bool isJson = parseError.error == QJsonParseError::NoError;
    const QString contentType = QString::fromUtf8(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray()).toLower();
    const QString head = rawText.left(512).trimmed().toLower();
    const bool isMarkup = !isJson && body.size() <= kMaxHighlightBytes
        && (contentType.contains("html") || contentType.contains("xml") || head.startsWith('<'));
    setHighlighting(isJson && body.size() <= kMaxHighlightBytes, isMarkup);
    m_raw->setPlainText(rawText);

    const bool renderable = body.size() <= kMaxHighlightBytes
        && (contentType.contains("html") || head.startsWith("<!doctype html") || head.startsWith("<html"));
    m_renderBase = reply->url();
    abortRenderImages();
    if (renderable) {
        m_render->document()->setBaseUrl(m_renderBase);
        m_render->setHtml(rawText);
        loadRenderStyles(rawText);
    } else {
        m_render->clear();
    }
    m_tabs->setTabVisible(3, renderable);

    if (isJson) {
        m_pretty->setPlainText(prettyJson(rawText, true));
        m_pretty->resetFolds(true);
        m_tabs->setCurrentIndex(0);
    } else {
        m_pretty->setPlainText(rawText);
        m_pretty->resetFolds(false);
        m_tabs->setCurrentIndex(isMarkup ? 0 : 1); // HTML / XML open on Pretty (coloured), other text on Raw
    }
    updatePathBar();

    m_current = 0;
    updateSearch(false);
    if (isVisible()) {
        if (m_searchBar->isVisible())
            m_query->setFocus();
        else
            m_tabs->currentWidget()->setFocus();
    }
    emit completed(m_pending, code, reason, ms, body.size());
    reply->deleteLater();
}
