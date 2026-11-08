#include "HistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

HistoryStore::HistoryStore(const QString &dir)
    : m_dir(dir)
{
}

QList<HistoryEntry> HistoryStore::loadAll() const
{
    QList<HistoryEntry> out;
    const QDir dir(m_dir);
    const auto names = dir.entryList({"*.json"}, QDir::Files);
    for (const QString &name : names) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;

        const QJsonObject root = doc.object();
        const QJsonObject result = root.value("result").toObject();
        HistoryEntry entry;
        entry.file = file.fileName();
        entry.time = QDateTime::fromString(root.value("time").toString(), Qt::ISODate);
        entry.request = root.value("request").toObject();
        entry.status = result.value("status").toInt();
        entry.reason = result.value("reason").toString();
        entry.ms = static_cast<qint64>(result.value("ms").toDouble());
        entry.size = static_cast<qint64>(result.value("size").toDouble());
        out.append(entry);
    }
    std::sort(out.begin(), out.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        return a.time > b.time;
    });
    return out;
}

int HistoryStore::clearAll() const
{
    int removed = 0;
    const QDir dir(m_dir);
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files)) {
        if (QFile::remove(dir.filePath(name)))
            ++removed;
    }
    return removed;
}

bool HistoryStore::remove(const QString &file) const
{
    // Only ever delete a history file that lives in the history folder
    return QFileInfo(file).absolutePath() == QFileInfo(m_dir).absoluteFilePath() && QFile::remove(file);
}

bool HistoryStore::save(HistoryEntry &entry) const
{
    if (!QDir().mkpath(m_dir))
        return false;

    if (entry.file.isEmpty()) {
        const QString host = QUrl(entry.request.value("url").toString(), QUrl::TolerantMode).host();
        QString slug = host.isEmpty() ? QStringLiteral("request") : host;
        slug.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
        const QString base = QString("%1_%2_%3")
                                 .arg(entry.time.toString("yyyyMMdd-HHmmss-zzz"),
                                      entry.request.value("method").toString(), slug);
        QString path = m_dir + "/" + base + ".json";
        for (int i = 2; QFileInfo::exists(path); ++i)
            path = m_dir + "/" + base + "-" + QString::number(i) + ".json";
        entry.file = path;
    }

    QJsonObject result;
    result["status"] = entry.status;
    result["reason"] = entry.reason;
    result["ms"] = static_cast<double>(entry.ms);
    result["size"] = static_cast<double>(entry.size);

    QJsonObject root;
    root["time"] = entry.time.toString(Qt::ISODate);
    root["request"] = entry.request;
    root["result"] = result;

    QSaveFile file(entry.file);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}
