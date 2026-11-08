#pragma once

#include "Theme.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QTimer;
class QListWidget;

class ThemePicker : public QDialog
{
    Q_OBJECT

public:
    explicit ThemePicker(QWidget *parent);

    void accept() override;
    void reject() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void refill();
    void step(int delta);

    QLineEdit *m_search;
    QLabel *m_count;
    QListWidget *m_list;
    Theme m_original;
    QTimer *m_previewTimer = nullptr;
    int m_pending = -1;
    int m_filter = 0;
};
