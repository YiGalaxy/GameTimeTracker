#include "statswidget.h"
#include "datamanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QPushButton>
#include <QFont>
#include <QDebug>
#include <algorithm>

StatsWidget::StatsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void StatsWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 12, 16, 12);

    // 标题行
    auto* headerLayout = new QHBoxLayout;

    m_titleLabel = new QLabel("📊 游戏时间统计");
    QFont titleFont;
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel);

    headerLayout->addStretch();

    auto* closeBtn = new QPushButton("✕");
    closeBtn->setFixedSize(28, 28);
    closeBtn->setStyleSheet("QPushButton { border: none; font-size: 16px; color: #999; } "
                            "QPushButton:hover { color: #333; }");
    connect(closeBtn, &QPushButton::clicked, this, &StatsWidget::closeRequested);
    headerLayout->addWidget(closeBtn);

    mainLayout->addLayout(headerLayout);

    // 今日总时长
    m_totalLabel = new QLabel;
    m_totalLabel->setStyleSheet(
        "QLabel { background-color: #E8F5E9; padding: 12px; border-radius: 6px; "
        "font-size: 18px; font-weight: bold; color: #2E7D32; }");
    m_totalLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_totalLabel);

    // 筛选器
    auto* filterLayout = new QHBoxLayout;
    filterLayout->addWidget(new QLabel("统计周期:"));

    m_periodCombo = new QComboBox;
    m_periodCombo->addItem("今日", "today");
    m_periodCombo->addItem("本周", "week");
    m_periodCombo->addItem("本月", "month");
    m_periodCombo->addItem("近7天", "7days");
    m_periodCombo->addItem("近30天", "30days");
    m_periodCombo->addItem("自定义", "custom");
    connect(m_periodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatsWidget::onFilterChanged);
    filterLayout->addWidget(m_periodCombo);

    m_fromDate = new QDateEdit(QDate::currentDate());
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setVisible(false);
    filterLayout->addWidget(new QLabel("从:"));
    filterLayout->addWidget(m_fromDate);

    m_toDate = new QDateEdit(QDate::currentDate());
    m_toDate->setCalendarPopup(true);
    m_toDate->setVisible(false);
    filterLayout->addWidget(new QLabel("到:"));
    filterLayout->addWidget(m_toDate);

    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // 统计表格
    m_statsTable = new QTableWidget;
    m_statsTable->setColumnCount(4);
    m_statsTable->setHorizontalHeaderLabels({"游戏名称", "游玩时长", "游玩次数", "占比"});
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    m_statsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_statsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_statsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_statsTable->setAlternatingRowColors(true);
    m_statsTable->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_statsTable);

    // 底部汇总
    m_summaryLabel = new QLabel;
    m_summaryLabel->setStyleSheet("color: #666; padding: 4px;");
    mainLayout->addWidget(m_summaryLabel);
}

void StatsWidget::onFilterChanged()
{
    QString mode = m_periodCombo->currentData().toString();
    bool custom = (mode == "custom");
    m_fromDate->setVisible(custom);
    m_toDate->setVisible(custom);
    // 如果切换到自定义时，查找对应的 label 也同样切换
    // 简单处理：使用 findChildren 不合适，直接通过布局索引
    loadStats();
}

void StatsWidget::refresh()
{
    loadStats();
}

void StatsWidget::loadStats()
{
    QString mode = m_periodCombo->currentData().toString();
    QList<TimeStats> stats;
    QDate today = QDate::currentDate();
    QString rangeText;

    if (mode == "today") {
        stats = DataManager::instance().getDailyStats(today);
        rangeText = "今日";
    } else if (mode == "week") {
        // 本周一为起始
        QDate monday = today.addDays(1 - today.dayOfWeek());
        stats = DataManager::instance().getWeeklyStats(monday);
        rangeText = QString("本周 (%1 ~ %2)").arg(monday.toString("MM/dd"), today.toString("MM/dd"));
    } else if (mode == "month") {
        stats = DataManager::instance().getMonthlyStats(today.year(), today.month());
        rangeText = QString("%1年%2月").arg(today.year()).arg(today.month());
    } else if (mode == "7days") {
        QDate from = today.addDays(-6);
        stats = DataManager::instance().getStatsByRange(from, today);
        rangeText = QString("近7天 (%1 ~ %2)").arg(from.toString("MM/dd"), today.toString("MM/dd"));
    } else if (mode == "30days") {
        QDate from = today.addDays(-29);
        stats = DataManager::instance().getStatsByRange(from, today);
        rangeText = QString("近30天 (%1 ~ %2)").arg(from.toString("MM/dd"), today.toString("MM/dd"));
    } else if (mode == "custom") {
        QDate from = m_fromDate->date();
        QDate to = m_toDate->date();
        if (from > to) std::swap(from, to);
        stats = DataManager::instance().getStatsByRange(from, to);
        rangeText = QString("%1 ~ %2").arg(from.toString("yyyy/MM/dd"), to.toString("yyyy/MM/dd"));
    }

    // 计算总时长
    int totalSecs = 0;
    for (const auto& s : stats)
        totalSecs += s.totalSeconds;

    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    m_totalLabel->setText(
        QString("🎯 %1 总游戏时间：%2小时%3分钟").arg(rangeText).arg(hours).arg(mins));

    // 填充表格
    m_statsTable->setRowCount(stats.size());
    for (int i = 0; i < stats.size(); ++i) {
        const auto& s = stats[i];

        auto* nameItem = new QTableWidgetItem(s.gameName);
        m_statsTable->setItem(i, 0, nameItem);

        auto* durItem = new QTableWidgetItem(s.durationStr());
        durItem->setTextAlignment(Qt::AlignCenter);
        m_statsTable->setItem(i, 1, durItem);

        auto* countItem = new QTableWidgetItem(QString::number(s.sessionCount));
        countItem->setTextAlignment(Qt::AlignCenter);
        m_statsTable->setItem(i, 2, countItem);

        // 占比
        double pct = totalSecs > 0 ? (100.0 * s.totalSeconds / totalSecs) : 0;
        auto* pctItem = new QTableWidgetItem(QString("%1%").arg(pct, 0, 'f', 1));
        pctItem->setTextAlignment(Qt::AlignCenter);
        m_statsTable->setItem(i, 3, pctItem);
    }

    m_summaryLabel->setText(
        QString("共 %1 款游戏，%2 次游戏会话").arg(stats.size())
            .arg([&stats]() { int c = 0; for(auto&s:stats)c+=s.sessionCount; return c; }()));
}
