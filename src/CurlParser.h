#pragma once

#include <QJsonObject>
#include <QString>

struct CurlImport
{
    bool ok = false;
    QJsonObject request; // same shape as RequestTab::requestJson()
    QString note;        // something in the command that sift can't represent
};

bool looksLikeCurl(const QString &text);
CurlImport parseCurl(const QString &command);
