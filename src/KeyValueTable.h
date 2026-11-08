#pragma once

#include <QList>
#include <QPair>
#include <QTableWidget>

class KeyValueTable : public QTableWidget
{
    Q_OBJECT

public:
    using Items = QList<QPair<QString, QString>>;

    explicit KeyValueTable(QWidget *parent = nullptr);

    struct Row
    {
        QString key;
        QString value;
        bool enabled = true;
    };

    Items items(bool enabledOnly = true) const;
    void setItems(const Items &items);
    QList<Row> rows() const;
    void setRows(const QList<Row> &rows);
    int activeCount() const;
    // Row under the mouse (-1 for none or for the empty "Add" row) and whether the mouse is on its trash icon
    int hoverRow() const { return m_hoverRow; }
    bool overTrash() const { return m_overTrash; }
    QRect trashRect(int row) const;

signals:
    void changed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool rowHasText(int row) const;
    void appendRow();
    void normalize();
    void setHover(int row, bool overTrash);
    void removeRowAt(int row);

    bool m_busy = false;
    int m_hoverRow = -1;
    bool m_overTrash = false;
};
