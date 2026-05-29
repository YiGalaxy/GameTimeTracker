#ifndef GAMELISTWIDGET_H
#define GAMELISTWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class GameListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GameListWidget(QWidget *parent = nullptr);

    // 刷新游戏列表
    void refresh();

signals:
    void closeRequested();
    void gameListChanged();

private slots:
    void onAddGame();
    void onRemoveGame();
    void onCellSelected(int row, int col);

private:
    void setupUI();
    void loadGameList();

    QTableWidget* m_gameTable;
    QLineEdit* m_gameNameEdit;
    QLineEdit* m_processNameEdit;
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    QLabel* m_statusLabel;
    QLabel* m_hintLabel;
};

#endif // GAMELISTWIDGET_H
