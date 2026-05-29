#ifndef STATSWIDGET_H
#define STATSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include <QComboBox>
#include <QDateEdit>

class StatsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StatsWidget(QWidget *parent = nullptr);

    // 刷新统计数据
    void refresh();

signals:
    void closeRequested();

private slots:
    void onFilterChanged();

private:
    void setupUI();
    void loadStats();

    QLabel* m_titleLabel;
    QLabel* m_totalLabel;       // "今日游戏X小时Y分钟"
    QComboBox* m_periodCombo;   // 今日/本周/本月/自定义
    QDateEdit* m_fromDate;
    QDateEdit* m_toDate;
    QTableWidget* m_statsTable;
    QLabel* m_summaryLabel;
};

#endif // STATSWIDGET_H
