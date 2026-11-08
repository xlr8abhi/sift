#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

struct BuiltRequest
{
    QString method;
    QUrl url;
    QList<QPair<QString, QString>> headers;
    QByteArray body;
};
