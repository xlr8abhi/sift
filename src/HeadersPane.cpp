#include "HeadersPane.h"

#include "FoldingEdit.h"
#include "ResponseWidgets.h"
#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextTable>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <QVBoxLayout>

namespace {

enum Group { Content, Caching, Security, Cookies, Other, GroupCount };

const char *groupTitle(int group)
{
    switch (group) {
    case Content:
        return QT_TRANSLATE_NOOP("HeadersPane", "Content");
    case Caching:
        return QT_TRANSLATE_NOOP("HeadersPane", "Caching");
    case Security:
        return QT_TRANSLATE_NOOP("HeadersPane", "Security");
    case Cookies:
        return QT_TRANSLATE_NOOP("HeadersPane", "Cookies");
    }
    return QT_TRANSLATE_NOOP("HeadersPane", "Other");
}

int groupOf(const QString &lowerName)
{
    static const QSet<QString> content = {"content-type",     "content-encoding", "content-length", "content-language",
                                          "content-disposition", "content-range", "content-md5",    "transfer-encoding",
                                          "accept-ranges",    "link",             "location"};
    static const QSet<QString> caching = {"cache-control", "date", "expires", "etag", "last-modified", "age", "vary", "pragma"};
    if (lowerName == "set-cookie")
        return Cookies;
    if (content.contains(lowerName))
        return Content;
    if (caching.contains(lowerName))
        return Caching;
    if (lowerName.startsWith("content-security-policy") || lowerName.startsWith("access-control-")
        || lowerName.startsWith("cross-origin-") || lowerName.startsWith("x-frame-") || lowerName == "x-xss-protection"
        || lowerName == "x-content-type-options" || lowerName == "strict-transport-security"
        || lowerName == "referrer-policy" || lowerName == "permissions-policy" || lowerName == "report-to"
        || lowerName == "nel")
        return Security;
    return Other;
}

QString mix(const QColor &a, const QColor &b, qreal t)
{
    QColor out = QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                                  a.blueF() * (1 - t) + b.blueF() * t);
    return out.name();
}

// One Set-Cookie value split into "name=value" and its attributes
QStringList splitCookies(const QString &value)
{
    QStringList out;
    // joined by newline, or by ", " when the server sent one line (the lookahead skips "expires=Sun, 08 Nov")
    static const QRegularExpression separator("\n|,\\s*(?=[^;,=\\s]+=)");
    for (const QString &part : value.split(separator, Qt::SkipEmptyParts))
        out << part.trimmed();
    return out;
}

} // namespace

QString HeadersPane::properName(const QString &name)
{
    static const QHash<QString, QString> special = {
        {"etag", "ETag"},   {"www-authenticate", "WWW-Authenticate"}, {"x-xss-protection", "X-XSS-Protection"},
        {"p3p", "P3P"},     {"content-md5", "Content-MD5"},           {"te", "TE"},
        {"dnt", "DNT"},     {"nel", "NEL"},                           {"x-ua-compatible", "X-UA-Compatible"},
        {"cf-ray", "CF-Ray"}, {"x-request-id", "X-Request-ID"},
    };
    const QString lower = name.toLower();
    if (special.contains(lower))
        return special.value(lower);
    QStringList parts = lower.split('-');
    for (QString &part : parts) {
        if (!part.isEmpty())
            part[0] = part.at(0).toUpper();
    }
    return parts.join('-');
}

HeadersPane::HeadersPane(QPlainTextEdit *raw, QWidget *parent)
    : QWidget(parent)
    , m_raw(raw)
{
    auto make = [this](const QString &text, bool checkable) {
        auto *button = new QToolButton;
        button->setText(text);
        button->setCheckable(checkable);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setStyleSheet("QToolButton { border: none; border-radius: 5px; padding: 3px 9px; color: palette(placeholder-text); }"
                              "QToolButton:hover { color: palette(text); }"
                              "QToolButton:checked { color: palette(text); background: palette(midlight); }");
        return button;
    };
    m_tableBtn = make(tr("Table"), true);
    m_rawBtn = make(tr("Raw"), true);
    m_tableBtn->setChecked(true);
    auto *modes = new QButtonGroup(this);
    modes->setExclusive(true);
    modes->addButton(m_tableBtn);
    modes->addButton(m_rawBtn);
    m_groupBtn = make(tr("Groups"), true);
    m_groupBtn->setChecked(true);
    m_groupBtn->setToolTip(tr("Group the headers (Content, Caching, Security, Cookies)"));

    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter headers"));
    m_filter->setClearButtonEnabled(true);

    m_copyBtn = new IconButton(IconButton::Kind::Copy);
    m_copyBtn->setToolTip(tr("Copy all headers as text"));

    auto *strip = new QHBoxLayout;
    strip->setContentsMargins(8, 6, 8, 6);
    strip->setSpacing(4);
    strip->addWidget(m_tableBtn);
    strip->addWidget(m_rawBtn);
    strip->addSpacing(8);
    strip->addWidget(m_filter, 1);
    strip->addWidget(m_groupBtn);
    strip->addWidget(m_copyBtn);

    m_table = new QTextBrowser;
    m_table->setOpenLinks(false);
    m_table->setFont(m_raw->font());
    m_table->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_table->viewport()->installEventFilter(this);
    new KineticScroll(m_table);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_table);
    m_stack->addWidget(m_raw);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(strip);
    layout->addWidget(m_stack, 1);
    setFocusProxy(m_table);

    connect(m_tableBtn, &QToolButton::clicked, this, [this] { setMode(false); });
    connect(m_rawBtn, &QToolButton::clicked, this, [this] { setMode(true); });
    connect(m_filter, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_groupBtn, &QToolButton::toggled, this, [this](bool on) {
        m_grouped = on;
        rebuild();
    });
    connect(m_copyBtn, &QAbstractButton::clicked, this, [this] {
        QApplication::clipboard()->setText(rawText());
        QToolTip::showText(m_copyBtn->mapToGlobal(QPoint(0, m_copyBtn->height())), tr("Copied"), m_copyBtn, {}, 1200);
    });
    setMode(false);
}

bool HeadersPane::rawActive() const
{
    return m_stack->currentWidget() == m_raw;
}

void HeadersPane::setMode(bool raw)
{
    m_stack->setCurrentWidget(raw ? static_cast<QWidget *>(m_raw) : m_table);
    setFocusProxy(raw ? static_cast<QWidget *>(m_raw) : m_table);
    m_filter->setEnabled(!raw);
    m_groupBtn->setEnabled(!raw);
    m_tableBtn->setChecked(!raw);
    m_rawBtn->setChecked(raw);
}

void HeadersPane::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

QString HeadersPane::rawText() const
{
    return m_raw->toPlainText();
}

void HeadersPane::setHeaders(const QList<Pair> &pairs)
{
    QString text;
    m_entries.clear();
    for (const Pair &pair : pairs) {
        const QString name = QString::fromUtf8(pair.first);
        const QString value = QString::fromUtf8(pair.second);
        text += name + ": " + value + "\n";
        m_entries.append({properName(name), value, groupOf(name.toLower())});
    }
    m_raw->setPlainText(text);
    rebuild();
}

void HeadersPane::refresh()
{
    rebuild();
}

void HeadersPane::rebuild()
{
    const SemanticColors c = ThemeManager::instance().colors();
    const QPalette pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    const QString line = mix(base, text, 0.10);
    const QString strip = mix(base, text, 0.05);
    const QString pill = mix(base, text, 0.13);
    const QString dim = pal.color(QPalette::PlaceholderText).name();
    const QString nameColor = c.key.name();
    const QString valueColor = c.string.name();
    const QString numberColor = c.number.name();

    const QString query = m_filter->text().trimmed();
    auto matches = [&](const Entry &e) {
        return query.isEmpty() || e.name.contains(query, Qt::CaseInsensitive) || e.value.contains(query, Qt::CaseInsensitive);
    };

    auto row = [&](const QString &name, const QString &value) {
        return QString("<tr><td width=\"26%\" valign=\"top\" style=\"border-bottom:1px solid %1;\"><span style=\"color:%2;\">%3</span></td>"
                       "<td valign=\"top\" style=\"border-bottom:1px solid %1;\">%4</td></tr>")
            .arg(line, nameColor, name.toHtmlEscaped(), value);
    };

    auto plainValue = [&](const QString &value) {
        bool numeric = !value.isEmpty();
        for (const QChar ch : value) {
            if (!ch.isDigit())
                numeric = false;
        }
        return QString("<span style=\"color:%1;\">%2</span>").arg(numeric ? numberColor : valueColor, value.toHtmlEscaped());
    };

    auto cookieValue = [&](const QString &value) {
        QString html;
        const QStringList cookies = splitCookies(value);
        for (int i = 0; i < cookies.size(); ++i) {
            const QStringList parts = cookies.at(i).split(';', Qt::SkipEmptyParts);
            if (parts.isEmpty())
                continue;
            html += "<p style=\"margin:0 0 6px 0;\">";
            const QString first = parts.first().trimmed();
            const int eq = first.indexOf('=');
            const QString cname = eq > 0 ? first.left(eq) : first;
            const QString cval = eq > 0 ? first.mid(eq + 1) : QString();
            html += QString("<b>%1</b><span style=\"color:%2;\">=%3</span><br>")
                        .arg(cname.toHtmlEscaped(), valueColor, cval.toHtmlEscaped());
            for (int p = 1; p < parts.size(); ++p) {
                QString attr = parts.at(p).trimmed();
                attr.replace('=', ' ');
                html += QString("<span style=\"color:%1; background-color:%2;\">&nbsp;%3&nbsp;</span> ")
                            .arg(dim, pill, attr.toHtmlEscaped());
            }
            html += "</p>";
        }
        return html.isEmpty() ? plainValue(value) : html;
    };

    QString html = "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\">";
    m_shown.clear();
    int shown = 0;
    auto addEntry = [&](int index) {
        const Entry &e = m_entries.at(index);
        const QString lower = e.name.toLower();
        html += row(e.name, lower == "set-cookie" ? cookieValue(e.value) : plainValue(e.value));
        m_shown.append(index);
        ++shown;
    };

    if (m_grouped) {
        for (int group = 0; group < GroupCount; ++group) {
            QList<int> rows;
            for (int i = 0; i < m_entries.size(); ++i) {
                if (m_entries.at(i).group == group && matches(m_entries.at(i)))
                    rows << i;
            }
            if (rows.isEmpty())
                continue;
            QString title = tr(groupTitle(group));
            if (group == Cookies) {
                int cookies = 0;
                for (int i : rows)
                    cookies += splitCookies(m_entries.at(i).value).size();
                title += QString(" \u00b7 %1").arg(cookies);
            }
            html += QString("<tr><td colspan=\"2\" bgcolor=\"%1\" style=\"border-bottom:1px solid %2;\">"
                            "<small><span style=\"color:%3;\">%4</span></small></td></tr>")
                        .arg(strip, line, dim, title.toHtmlEscaped());
            m_shown.append(-1);
            for (int i : rows)
                addEntry(i);
        }
    } else {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (matches(m_entries.at(i)))
                addEntry(i);
        }
    }
    html += "</table>";
    if (shown == 0) {
        html = QString("<p style=\"color:%1;\">&nbsp;%2</p>")
                   .arg(dim, m_entries.isEmpty() ? tr("No headers yet.").toHtmlEscaped() : tr("No header matches.").toHtmlEscaped());
    }

    const int scroll = m_table->verticalScrollBar()->value();
    m_table->setHtml(html);
    m_table->verticalScrollBar()->setValue(scroll);
}

bool HeadersPane::eventFilter(QObject *watched, QEvent *event)
{
    // Ctrl+wheel zoom with a floor and a ceiling (Qt's own zoom has none and ends in a freeze or crash)
    if (watched == m_table->viewport() && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            constexpr qreal kMinPt = 6.0;
            constexpr qreal kMaxPt = 36.0;
            QFont font = m_table->font();
            const qreal size = qBound(kMinPt, font.pointSizeF() + wheel->angleDelta().y() / 120.0, kMaxPt);
            if (!qFuzzyCompare(size, font.pointSizeF())) {
                font.setPointSizeF(size);
                m_table->setFont(font);
            }
            return true;
        }
    }
    if (watched == m_table->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && (mouse->modifiers() & Qt::ControlModifier)) {
            copyRow(mouse->position().toPoint());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HeadersPane::copyRow(const QPoint &viewportPos)
{
    QTextCursor cursor = m_table->cursorForPosition(viewportPos);
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    const int row = table->cellAt(cursor).row();
    if (row < 0 || row >= m_shown.size() || m_shown.at(row) < 0)
        return;
    QApplication::clipboard()->setText(m_entries.at(m_shown.at(row)).value);
    QToolTip::showText(m_table->viewport()->mapToGlobal(viewportPos), tr("Value copied"), m_table, {}, 1200);
}
