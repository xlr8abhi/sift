#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

struct HistoryEntry
{
    QString file;
    QDateTime time;
    QJsonObject request;
    int status = 0;
    QString reason;
    qint64 ms = 0;
    qint64 size = 0;
};

class HistoryStore
{
public:
    explicit HistoryStore(const QString &dir);

    QString dir() const { return m_dir; }
    QList<HistoryEntry> loadAll() const;
    bool save(HistoryEntry &entry) const;
    int clearAll() const;
    bool remove(const QString &file) const;

private:
    QString m_dir;
};
