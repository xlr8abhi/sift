#include "AppFont.h"
#include "RequestPreview.h"
#include <QToolTip>
#include <QToolButton>
#include <QStackedWidget>
#include <QFrame>
#include <QButtonGroup>
#include "ResponseWidgets.h"

#include "JsonHighlighter.h"
#include "Theme.h"

#include <QClipboard>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QEventLoop>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>

namespace {

using HeaderList = QList<QPair<QString, QString>>;

const HeaderList &defaultHeaders()
{
    static bool probed = false;
    static HeaderList defaults;
    if (probed)
        return defaults;
    probed = true;

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost))
        return defaults;

    QNetworkAccessManager nam;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, &loop, [&, socket] {
            static QByteArray data;
            data += socket->readAll();
            if (!data.contains("\r\n\r\n"))
                return;
            const QList<QByteArray> lines = data.split('\r');
            for (const QByteArray &raw : lines) {
                const QByteArray line = raw.trimmed();
                const int colon = line.indexOf(':');
                if (colon <= 0)
                    continue;
                const QString name = QString::fromLatin1(line.left(colon));
                if (name.compare("Host", Qt::CaseInsensitive) == 0)
                    continue;
                defaults.append({name, QString::fromLatin1(line.mid(colon + 1).trimmed())});
            }
            data.clear();
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->flush();
            loop.quit();
        });
    });

    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()))));
    timeout.start(1500);
    loop.exec();
    reply->abort();
    reply->deleteLater();
    return defaults;
}

QString hostHeader(const QUrl &url)
{
    QString host = url.host();
    if (url.port() != -1)
        host += ":" + QString::number(url.port());
    return host;
}

QString httpText(const BuiltRequest &request)
{
    QString path = request.url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
        path = "/";
    if (request.url.hasQuery())
        path += "?" + request.url.query(QUrl::FullyEncoded);

    HeaderList headers;
    headers.append({"Host", hostHeader(request.url)});
    headers.append(request.headers);
    if (!request.body.isEmpty())
        headers.append({"Content-Length", QString::number(request.body.size())});

    for (const auto &pair : defaultHeaders()) {
        bool present = false;
        for (const auto &have : headers) {
            if (have.first.compare(pair.first, Qt::CaseInsensitive) == 0)
                present = true;
        }
        if (!present)
            headers.append(pair);
    }

    QString text = request.method + " " + path + " HTTP/1.1\n";
    for (const auto &pair : headers)
        text += pair.first + ": " + pair.second + "\n";
    text += "\n";
    text += QString::fromUtf8(request.body);
    return text;
}

QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace('\'', "'\\''");
    return "'" + quoted + "'";
}

QString curlText(const BuiltRequest &request)
{
    QStringList parts;
    parts << "curl " + shellQuote(request.url.toString(QUrl::FullyEncoded));
    if (request.method != "GET" || !request.body.isEmpty())
        parts << "-X " + request.method;
    for (const auto &pair : request.headers)
        parts << "-H " + shellQuote(pair.first + ": " + pair.second);
    if (!request.body.isEmpty())
        parts << "--data-raw " + shellQuote(QString::fromUtf8(request.body));
    return parts.join(" \\\n  ");
}

class HttpHighlighter : public JsonHighlighter
{
public:
    explicit HttpHighlighter(QTextDocument *document)
        : JsonHighlighter(document)
    {
        setActive(true);
        const SemanticColors c = ThemeManager::instance().colors();
        m_c = c;
        m_dim = QGuiApplication::palette().color(QPalette::PlaceholderText);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        enum { Start = -1, Headers = 1, AfterBlank = 5, Json = 2, Form = 3, Plain = 4 };
        const int previous = previousBlockState();

        if (previous == Start) {
            requestLine(text);
            setCurrentBlockState(Headers);
            return;
        }

        if (previous == Headers) {
            if (text.isEmpty()) {
                setCurrentBlockState(AfterBlank);
                return;
            }
            headerLine(text);
            setCurrentBlockState(Headers);
            return;
        }

        int mode = previous;
        if (previous == AfterBlank) {
            const QString trimmed = text.trimmed();
            if (trimmed.startsWith('{') || trimmed.startsWith('['))
                mode = Json;
            else if (trimmed.contains('=') && !trimmed.contains(' '))
                mode = Form;
            else
                mode = Plain;
        }
        setCurrentBlockState(mode);

        if (mode == Json) {
            JsonHighlighter::highlightBlock(text);
        } else if (mode == Form) {
            int i = 0;
            while (i < text.size()) {
                int end = text.indexOf('&', i);
                if (end < 0)
                    end = text.size();
                const int eq = text.indexOf('=', i);
                if (eq >= 0 && eq < end) {
                    setFormat(i, eq - i, m_c.key);
                    setFormat(eq, 1, m_dim);
                    setFormat(eq + 1, end - eq - 1, m_c.string);
                }
                if (end < text.size())
                    setFormat(end, 1, m_dim);
                i = end + 1;
            }
        }
    }

private:
    void requestLine(const QString &text)
    {
        const int firstSpace = text.indexOf(' ');
        const int lastSpace = text.lastIndexOf(' ');
        if (firstSpace <= 0 || lastSpace <= firstSpace)
            return;

        QTextCharFormat method;
        method.setForeground(methodColor(text.left(firstSpace)));
        method.setFontWeight(QFont::Bold);
        setFormat(0, firstSpace, method);

        const QString target = text.mid(firstSpace + 1, lastSpace - firstSpace - 1);
        const int q = target.indexOf('?');
        const int pathLen = q < 0 ? target.size() : q;
        setFormat(firstSpace + 1, pathLen, m_c.accent);
        if (q >= 0)
            setFormat(firstSpace + 1 + q, target.size() - q, m_c.string);
        setFormat(lastSpace + 1, text.size() - lastSpace - 1, m_dim);
    }

    void headerLine(const QString &text)
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

    SemanticColors m_c;
    QColor m_dim;
};

class CurlHighlighter : public QSyntaxHighlighter
{
public:
    explicit CurlHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        m_c = ThemeManager::instance().colors();
        m_dim = QGuiApplication::palette().color(QPalette::PlaceholderText);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        enum Flag { None = 0, Header = 1, Data = 2, Method = 3, Url = 4 };
        const int previous = qMax(0, previousBlockState());
        bool inQuote = previous & 1;
        int flag = previous >> 1;

        const int n = text.size();
        int i = 0;
        while (i < n) {
            const QChar ch = text.at(i);

            if (inQuote) {
                int end = i;
                while (end < n && text.at(end) != '\'')
                    ++end;
                if (flag == Header) {
                    const int colon = text.indexOf(':', i);
                    if (colon >= 0 && colon < end) {
                        setFormat(i, colon - i, m_c.key);
                        setFormat(colon, 1, m_dim);
                        setFormat(colon + 1, end - colon - 1, m_c.string);
                    } else {
                        setFormat(i, end - i, m_c.string);
                    }
                } else if (flag == Url) {
                    setFormat(i, end - i, m_c.accent);
                } else {
                    setFormat(i, end - i, m_c.string);
                }
                if (end < n) {
                    inQuote = false;
                    flag = None;
                    i = end + 1;
                } else {
                    i = end;
                }
                continue;
            }

            if (ch.isSpace()) {
                ++i;
                continue;
            }
            if (ch == '\\') {
                if (i + 1 < n && text.at(i + 1) == '\'') {
                    setFormat(i, 2, m_c.string);
                    i += 2;
                } else {
                    setFormat(i, 1, m_dim);
                    ++i;
                }
                continue;
            }
            if (ch == '\'') {
                inQuote = true;
                ++i;
                continue;
            }

            int end = i;
            while (end < n && !text.at(end).isSpace() && text.at(end) != '\'' && text.at(end) != '\\')
                ++end;
            const QString word = text.mid(i, end - i);
            if (word == "curl") {
                setFormat(i, end - i, m_c.keyword);
                flag = Url;
            } else if (word.startsWith('-')) {
                setFormat(i, end - i, m_c.keyword);
                flag = word == "-X" ? Method : word == "-H" ? Header : word == "--data-raw" ? Data : None;
            } else if (flag == Method) {
                QTextCharFormat method;
                method.setForeground(methodColor(word));
                method.setFontWeight(QFont::Bold);
                setFormat(i, end - i, method);
                flag = None;
            }
            i = end;
        }

        setCurrentBlockState((inQuote ? 1 : 0) | (flag << 1));
    }

private:
    SemanticColors m_c;
    QColor m_dim;
};

QPlainTextEdit *makeView(const QString &text)
{
    auto *edit = new QPlainTextEdit(text);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setFont(AppFont::mono());
    return edit;
}

}

RequestPreview::RequestPreview(QWidget *parent)
    : QWidget(parent)
{
    m_http = makeView(QString());
    m_curl = makeView(QString());
    new HttpHighlighter(m_http->document());
    new CurlHighlighter(m_curl->document());

    // Ctrl+scroll zoom with a floor and ceiling (Qt's own has no lower limit).
    m_http->viewport()->installEventFilter(this);
    m_curl->viewport()->installEventFilter(this);

    // A strip on top: the cURL | HTTP switch on the left, Copy on the far right
    auto *segment = new QFrame;
    segment->setObjectName("previewSeg");
    segment->setStyleSheet("QFrame#previewSeg { background: palette(alternate-base); border-radius: 8px; }"
                           "QToolButton { border: none; border-radius: 6px; padding: 3px 14px; color: palette(placeholder-text); }"
                           "QToolButton:hover { color: palette(text); }"
                           "QToolButton:checked { color: palette(text); background: palette(midlight); }");
    auto makeSwitch = [](const QString &text) {
        auto *button = new QToolButton;
        button->setText(text);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    m_curlBtn = makeSwitch(tr("cURL"));
    m_httpBtn = makeSwitch(tr("HTTP"));
    auto *segmentLayout = new QHBoxLayout(segment);
    segmentLayout->setContentsMargins(2, 2, 2, 2);
    segmentLayout->setSpacing(2);
    segmentLayout->addWidget(m_curlBtn);
    segmentLayout->addWidget(m_httpBtn);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_curlBtn);
    group->addButton(m_httpBtn);
    connect(m_curlBtn, &QToolButton::clicked, this, [this] { setView(false); });
    connect(m_httpBtn, &QToolButton::clicked, this, [this] { setView(true); });

    m_copy = new IconButton(IconButton::Kind::Copy);
    m_copy->setToolTip(tr("Copy (C)"));
    connect(m_copy, &QAbstractButton::clicked, this, &RequestPreview::copyCurrent);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_curl);
    m_stack->addWidget(m_http);

    // Plain "C" copies the visible view, only while focus is inside this tab.
    // Ctrl+C is untouched, so it still copies a selection.
    auto *copyKey = new QShortcut(QKeySequence(Qt::Key_C), this);
    copyKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyKey, &QShortcut::activated, this, &RequestPreview::copyCurrent);

    auto *strip = new QHBoxLayout;
    strip->setContentsMargins(8, 6, 8, 6);
    strip->addWidget(segment);
    strip->addStretch(1);
    strip->addWidget(m_copy);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(strip);
    layout->addWidget(m_stack, 1);
    setView(false);
}

void RequestPreview::setView(bool http)
{
    m_stack->setCurrentIndex(http ? 1 : 0);
    m_curlBtn->setChecked(!http);
    m_httpBtn->setChecked(http);
}

bool RequestPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        auto *edit = qobject_cast<QWidget *>(watched)->parentWidget();
        if (edit && (wheel->modifiers() & Qt::ControlModifier)) {
            QFont font = edit->font();
            const qreal size = qBound(6.0, font.pointSizeF() + wheel->angleDelta().y() / 120.0, 36.0);
            if (!qFuzzyCompare(size, font.pointSizeF())) {
                font.setPointSizeF(size);
                edit->setFont(font);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RequestPreview::setRequest(const BuiltRequest &request)
{
    m_http->setPlainText(httpText(request));
    m_curl->setPlainText(curlText(request));
}

void RequestPreview::clear()
{
    m_http->clear();
    m_curl->clear();
}

void RequestPreview::focusView()
{
    if (auto *view = m_stack->currentWidget())
        view->setFocus();
}

void RequestPreview::copyCurrent()
{
    auto *edit = qobject_cast<QPlainTextEdit *>(m_stack->currentWidget());
    if (!edit)
        return;
    QGuiApplication::clipboard()->setText(edit->toPlainText());
    QToolTip::showText(m_copy->mapToGlobal(QPoint(0, m_copy->height())), tr("Copied"), m_copy, {}, 1200);
}
