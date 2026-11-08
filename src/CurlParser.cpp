#include "CurlParser.h"

#include <QByteArray>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Shell-style split: '...', "...", $'...' and backslash escapes. A backslash
// followed by whitespace outside quotes is a line continuation.
QStringList tokenize(const QString &text)
{
    QStringList tokens;
    QString current;
    bool have = false;
    const int n = text.size();
    int i = 0;

    auto flush = [&] {
        if (have)
            tokens << current;
        current.clear();
        have = false;
    };

    while (i < n) {
        const QChar ch = text.at(i);

        if (ch.isSpace()) {
            flush();
            ++i;
        } else if (ch == '\\') {
            if (i + 1 >= n) {
                ++i;
            } else if (text.at(i + 1).isSpace()) {
                // continuation: swallow the backslash and the whitespace run after it
                ++i;
                while (i < n && text.at(i).isSpace())
                    ++i;
                // a continuation is whitespace between tokens, not part of one
                flush();
            } else {
                current += text.at(i + 1);
                have = true;
                i += 2;
            }
        } else if (ch == '\'') {
            have = true;
            ++i;
            while (i < n && text.at(i) != '\'')
                current += text.at(i++);
            ++i;
        } else if (ch == '"') {
            have = true;
            ++i;
            while (i < n && text.at(i) != '"') {
                if (text.at(i) == '\\' && i + 1 < n && QStringLiteral("\"\\$`").contains(text.at(i + 1))) {
                    current += text.at(i + 1);
                    i += 2;
                } else if (text.at(i) == '\\' && i + 1 < n && text.at(i + 1) == '\n') {
                    i += 2;
                } else {
                    current += text.at(i++);
                }
            }
            ++i;
        } else if (ch == '$' && i + 1 < n && text.at(i + 1) == '\'') {
            have = true;
            i += 2;
            while (i < n && text.at(i) != '\'') {
                if (text.at(i) != '\\' || i + 1 >= n) {
                    current += text.at(i++);
                    continue;
                }
                const QChar e = text.at(i + 1);
                i += 2;
                switch (e.unicode()) {
                case 'n': current += '\n'; break;
                case 't': current += '\t'; break;
                case 'r': current += '\r'; break;
                case 'a': current += QChar(7); break;
                case 'b': current += '\b'; break;
                case 'f': current += '\f'; break;
                case 'v': current += '\v'; break;
                case 'e': current += QChar(27); break;
                case 'x':
                case 'u':
                case 'U': {
                    const int maxDigits = e == 'x' ? 2 : e == 'u' ? 4 : 8;
                    int digits = 0;
                    uint value = 0;
                    while (digits < maxDigits && i < n) {
                        bool ok = false;
                        const uint d = QString(text.at(i)).toUInt(&ok, 16);
                        if (!ok)
                            break;
                        value = value * 16 + d;
                        ++digits;
                        ++i;
                    }
                    if (digits > 0) {
                        if (e == 'x')
                            current += QChar(static_cast<ushort>(value));
                        else
                            current += QString::fromUcs4(reinterpret_cast<const char32_t *>(&value), 1);
                    }
                    break;
                }
                default:
                    current += e; // \\ \' \" and unknown escapes
                    break;
                }
            }
            ++i;
        } else {
            current += ch;
            have = true;
            ++i;
        }
    }
    flush();
    return tokens;
}

const QSet<QString> &longWithArg()
{
    static const QSet<QString> set = {
        "request", "header", "data", "data-raw", "data-binary", "data-ascii", "data-urlencode", "json", "user",
        "cookie", "user-agent", "referer", "url", "form", "output", "max-time", "connect-timeout", "proxy",
        "write-out", "cookie-jar", "retry", "limit-rate", "cacert", "cert", "key", "resolve", "interface",
        "upload-file", "dump-header", "config", "max-redirs", "proxy-user", "oauth2-bearer", "range",
        "time-cond", "output-dir", "retry-delay", "retry-max-time", "keepalive-time", "local-port",
        "socks5", "unix-socket", "http2-prior-knowledge-dummy", "form-string", "variable", "expand-data",
    };
    return set;
}

const QString kShortWithArg = QStringLiteral("XHdAeubcomxwFTrKEzUDCtyY");

QString shortToLong(QChar c)
{
    switch (c.unicode()) {
    case 'X': return "request";
    case 'H': return "header";
    case 'd': return "data";
    case 'A': return "user-agent";
    case 'e': return "referer";
    case 'u': return "user";
    case 'b': return "cookie";
    case 'F': return "form";
    case 'I': return "head";
    case 'G': return "get";
    default: return QString(c);
    }
}

QJsonArray rowsOf(const QList<QPair<QString, QString>> &pairs)
{
    QJsonArray array;
    for (const auto &pair : pairs) {
        QJsonObject row;
        row["key"] = pair.first;
        row["value"] = pair.second;
        row["enabled"] = true;
        array.append(row);
    }
    return array;
}

QList<QPair<QString, QString>> formPairs(const QString &body)
{
    QList<QPair<QString, QString>> pairs;
    for (const QString &part : body.split('&', Qt::SkipEmptyParts)) {
        const int eq = part.indexOf('=');
        const QString key = eq < 0 ? part : part.left(eq);
        const QString value = eq < 0 ? QString() : part.mid(eq + 1);
        pairs.append({QUrl::fromPercentEncoding(key.toUtf8().replace('+', ' ')),
                      QUrl::fromPercentEncoding(value.toUtf8().replace('+', ' '))});
    }
    return pairs;
}

}

bool looksLikeCurl(const QString &text)
{
    static const QRegularExpression re(R"(^\s*curl(\s|$))");
    return re.match(text).hasMatch() && text.trimmed().size() > 4;
}

CurlImport parseCurl(const QString &command)
{
    CurlImport result;
    const QStringList tokens = tokenize(command);
    if (tokens.isEmpty() || tokens.first() != "curl")
        return result;

    QString method;
    QString url;
    QList<QPair<QString, QString>> headers;
    QStringList dataParts;
    bool getMode = false;
    bool head = false;
    QStringList unsupported;

    auto addHeader = [&](const QString &line) {
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            if (line.endsWith(';')) // "Name;" means send an empty header
                headers.append({line.left(line.size() - 1).trimmed(), QString()});
            return;
        }
        headers.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
    };

    auto apply = [&](const QString &name, const QString &arg) {
        if (name == "request")
            method = arg.toUpper();
        else if (name == "header")
            addHeader(arg);
        else if (name == "data" || name == "data-raw" || name == "data-binary" || name == "data-ascii")
            dataParts << arg;
        else if (name == "data-urlencode") {
            const int eq = arg.indexOf('=');
            if (eq < 0)
                dataParts << QString::fromLatin1(QUrl::toPercentEncoding(arg));
            else if (eq == 0)
                dataParts << QString::fromLatin1(QUrl::toPercentEncoding(arg.mid(1)));
            else
                dataParts << arg.left(eq) + "=" + QString::fromLatin1(QUrl::toPercentEncoding(arg.mid(eq + 1)));
        } else if (name == "json") {
            dataParts << arg;
            headers.append({QString("Content-Type"), QString("application/json")});
            headers.append({QString("Accept"), QString("application/json")});
        } else if (name == "user") {
            headers.append({QString("Authorization"), "Basic " + QString::fromLatin1(arg.toUtf8().toBase64())});
        } else if (name == "cookie") {
            if (arg.contains('='))
                headers.append({QString("Cookie"), arg});
            else
                unsupported << "cookie file";
        } else if (name == "user-agent")
            headers.append({QString("User-Agent"), arg});
        else if (name == "referer")
            headers.append({QString("Referer"), arg});
        else if (name == "url")
            url = arg;
        else if (name == "form" || name == "form-string" || name == "upload-file")
            unsupported << "multipart / file upload";
    };

    for (int i = 1; i < tokens.size(); ++i) {
        const QString &token = tokens.at(i);

        if (token.startsWith("--") && token.size() > 2) {
            QString name = token.mid(2);
            QString arg;
            bool hasArg = false;
            const int eq = name.indexOf('=');
            if (eq > 0) {
                arg = name.mid(eq + 1);
                name = name.left(eq);
                hasArg = true;
            }
            if (name == "get") {
                getMode = true;
            } else if (name == "head") {
                head = true;
            } else if (longWithArg().contains(name)) {
                if (!hasArg && i + 1 < tokens.size())
                    arg = tokens.at(++i);
                apply(name, arg);
            }
            continue;
        }

        if (token.startsWith('-') && token.size() > 1) {
            for (int k = 1; k < token.size(); ++k) {
                const QChar flag = token.at(k);
                if (kShortWithArg.contains(flag)) {
                    QString arg = token.mid(k + 1);
                    if (arg.isEmpty() && i + 1 < tokens.size())
                        arg = tokens.at(++i);
                    apply(shortToLong(flag), arg);
                    break;
                }
                if (flag == 'G')
                    getMode = true;
                else if (flag == 'I')
                    head = true;
            }
            continue;
        }

        if (url.isEmpty())
            url = token;
    }

    if (url.isEmpty())
        return result;

    // Method.
    const QString body = dataParts.join('&');
    const bool hasBody = !body.isEmpty() && !getMode;
    if (getMode && !body.isEmpty()) {
        QUrl parsed(url, QUrl::TolerantMode);
        QString query = parsed.query(QUrl::FullyEncoded);
        parsed.setQuery(query.isEmpty() ? body : query + "&" + body, QUrl::TolerantMode);
        url = parsed.toString(QUrl::FullyEncoded);
    }
    if (head)
        unsupported << "HEAD";
    if (method.isEmpty())
        method = hasBody ? "POST" : "GET";
    static const QStringList known = {"GET", "POST", "PUT", "PATCH", "DELETE"};
    if (!known.contains(method)) {
        unsupported << method;
        method = hasBody ? "POST" : "GET";
    }

    // Body type, decided from Content-Type first and the body's shape second.
    QString contentType;
    for (const auto &pair : headers) {
        if (pair.first.compare("Content-Type", Qt::CaseInsensitive) == 0)
            contentType = pair.second.toLower();
    }

    QString bodyType = "none";
    QJsonObject bodyObject;
    if (hasBody) {
        const QString trimmed = body.trimmed();
        if (contentType.contains("json") || (contentType.isEmpty() && (trimmed.startsWith('{') || trimmed.startsWith('[')))) {
            bodyType = "json";
            bodyObject["text"] = body;
        } else if (contentType.contains("x-www-form-urlencoded") || (contentType.isEmpty() && body.contains('=') && !body.contains(' '))) {
            bodyType = "form";
            bodyObject["form"] = rowsOf(formPairs(body));
        } else {
            bodyType = "raw";
            bodyObject["text"] = body;
        }
    }
    bodyObject["type"] = bodyType;

    // An automatic Content-Type is added at send time, so drop the explicit one
    // when it only repeats what the chosen body type would send anyway.
    QList<QPair<QString, QString>> kept;
    for (const auto &pair : headers) {
        if (pair.first.compare("Content-Type", Qt::CaseInsensitive) == 0) {
            const QString v = pair.second.trimmed().toLower();
            if ((bodyType == "json" && v == "application/json") || (bodyType == "form" && v == "application/x-www-form-urlencoded"))
                continue;
        }
        kept.append(pair);
    }

    QList<QPair<QString, QString>> params;
    for (const auto &pair : QUrlQuery(QUrl(url, QUrl::TolerantMode)).queryItems(QUrl::FullyDecoded))
        params.append(pair);

    QJsonObject request;
    request["method"] = method;
    request["url"] = url;
    request["params"] = rowsOf(params);
    request["headers"] = rowsOf(kept);
    request["body"] = bodyObject;

    result.ok = true;
    result.request = request;
    unsupported.removeDuplicates();
    if (!unsupported.isEmpty())
        result.note = unsupported.join(", ");
    return result;
}
