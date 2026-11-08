#pragma once

#include "BuiltRequest.h"

#include <QWidget>

class QPlainTextEdit;
class IconButton;
class QStackedWidget;
class QToolButton;

class RequestPreview : public QWidget
{
    Q_OBJECT

public:
    explicit RequestPreview(QWidget *parent = nullptr);

    void setRequest(const BuiltRequest &request);
    void clear();
    void focusView();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void copyCurrent();

    void setView(bool http);

    IconButton *m_copy;
    QStackedWidget *m_stack;
    QToolButton *m_curlBtn;
    QToolButton *m_httpBtn;
    QPlainTextEdit *m_http;
    QPlainTextEdit *m_curl;
};
