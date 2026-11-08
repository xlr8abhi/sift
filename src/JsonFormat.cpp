#include "JsonFormat.h"

#include <QJsonDocument>

namespace {

bool hex4(const QString &text, int pos, uint &value)
{
    if (pos + 4 > text.size())
        return false;
    bool ok = false;
    value = text.mid(pos, 4).toUInt(&ok, 16);
    return ok;
}

}

QString prettyJson(const QString &text, bool decodeUnicode)
{
    QJsonParseError error;
    QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        return text;

    QString out;
    out.reserve(text.size() * 2);
    int depth = 0;
    bool inString = false;
    const int n = text.size();

    auto newline = [&] {
        out += '\n';
        out += QString(depth * 4, ' ');
    };

    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);

        if (inString) {
            if (ch == '\\' && i + 1 < n) {
                uint code = 0;
                if (decodeUnicode && text.at(i + 1) == 'u' && hex4(text, i + 2, code)) {
                    int used = 6;
                    QString decoded;
                    uint low = 0;
                    if (code >= 0xD800 && code < 0xDC00 && i + 12 <= n && text.at(i + 6) == '\\'
                        && text.at(i + 7) == 'u' && hex4(text, i + 8, low) && low >= 0xDC00 && low < 0xE000) {
                        decoded = QString(QChar(ushort(code))) + QChar(ushort(low));
                        used = 12;
                    } else if (code >= 0x20 && code != '"' && code != '\\' && !(code >= 0xD800 && code < 0xE000)) {
                        decoded = QString(QChar(ushort(code)));
                    }
                    if (!decoded.isEmpty()) {
                        out += decoded;
                        i += used - 1;
                        continue;
                    }
                }
                out += ch;
                out += text.at(++i);
                continue;
            }
            out += ch;
            if (ch == '"')
                inString = false;
            continue;
        }

        if (ch.isSpace())
            continue;

        if (ch == '"') {
            inString = true;
            out += ch;
        } else if (ch == '{' || ch == '[') {
            // keep empty containers on one line
            int j = i + 1;
            while (j < n && text.at(j).isSpace())
                ++j;
            if (j < n && (text.at(j) == '}' || text.at(j) == ']')) {
                out += ch;
                out += text.at(j);
                i = j;
            } else {
                out += ch;
                ++depth;
                newline();
            }
        } else if (ch == '}' || ch == ']') {
            --depth;
            newline();
            out += ch;
        } else if (ch == ',') {
            out += ch;
            newline();
        } else if (ch == ':') {
            out += QStringLiteral(": ");
        } else {
            out += ch;
        }
    }
    return out;
}
