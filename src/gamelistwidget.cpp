#include "gamelistwidget.h"
#include "datamanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>
#include <QFont>
#include <QDebug>

GameListWidget::GameListWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    loadGameList();
}

void GameListWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 12, 16, 12);

    // 标题栏
    auto* headerLayout = new QHBoxLayout;
    auto* titleLabel = new QLabel("🎮 游戏列表管理");
    QFont titleFont;
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    auto* closeBtn = new QPushButton("✕");
    closeBtn->setFixedSize(28, 28);
    closeBtn->setStyleSheet("QPushButton { border: none; font-size: 16px; color: #999; } "
                            "QPushButton:hover { color: #333; }");
    connect(closeBtn, &QPushButton::clicked, this, &GameListWidget::closeRequested);
    headerLayout->addWidget(closeBtn);
    mainLayout->addLayout(headerLayout);

    // 提示
    m_hintLabel = new QLabel(
        "💡 提示：进程名是游戏在任务管理器中显示的 .exe 名称。\n"
        "例如：原神 → YuanShen.exe，Apex → r5apex.exe，LOL → League of Legends.exe");
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet("color: #666; background-color: #FFF3E0; padding: 10px; "
                                "border-radius: 4px; font-size: 12px;");
    mainLayout->addWidget(m_hintLabel);

    // 添加表单
    auto* addGroup = new QGroupBox("添加新游戏");
    auto* formLayout = new QFormLayout(addGroup);

    m_gameNameEdit = new QLineEdit;
    m_gameNameEdit->setPlaceholderText("例如：原神");
    formLayout->addRow("游戏名称:", m_gameNameEdit);

    m_processNameEdit = new QLineEdit;
    m_processNameEdit->setPlaceholderText("例如：YuanShen.exe");
    formLayout->addRow("进程名称:", m_processNameEdit);

    m_addBtn = new QPushButton("➕ 添加游戏");
    m_addBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; padding: 8px 16px; "
        "border-radius: 4px; } QPushButton:hover { background-color: #45a049; }");
    connect(m_addBtn, &QPushButton::clicked, this, &GameListWidget::onAddGame);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_addBtn);

    m_removeBtn = new QPushButton("🗑 删除选中");
    m_removeBtn->setStyleSheet(
        "QPushButton { background-color: #f44336; color: white; padding: 8px 16px; "
        "border-radius: 4px; } QPushButton:hover { background-color: #d32f2f; }");
    connect(m_removeBtn, &QPushButton::clicked, this, &GameListWidget::onRemoveGame);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();

    formLayout->addRow("", btnLayout);
    mainLayout->addWidget(addGroup);

    // 游戏列表
    m_gameTable = new QTableWidget;
    m_gameTable->setColumnCount(2);
    m_gameTable->setHorizontalHeaderLabels({"游戏名称", "进程名称 (.exe)"});
    m_gameTable->horizontalHeader()->setStretchLastSection(true);
    m_gameTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_gameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_gameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_gameTable->setAlternatingRowColors(true);
    m_gameTable->verticalHeader()->setVisible(false);
    connect(m_gameTable, &QTableWidget::cellClicked, this, &GameListWidget::onCellSelected);
    mainLayout->addWidget(m_gameTable);

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("color: #666;");
    mainLayout->addWidget(m_statusLabel);
}

void GameListWidget::loadGameList()
{
    auto games = DataManager::instance().getGameList();
    m_gameTable->setRowCount(games.size());

    for (int i = 0; i < games.size(); ++i) {
        m_gameTable->setItem(i, 0, new QTableWidgetItem(games[i].first));
        m_gameTable->setItem(i, 1, new QTableWidgetItem(games[i].second));
    }

    m_statusLabel->setText(QString("共 %1 个游戏在监控中").arg(games.size()));
}

void GameListWidget::onAddGame()
{
    QString gameName = m_gameNameEdit->text().trimmed();
    QString procName = m_processNameEdit->text().trimmed();

    if (gameName.isEmpty() || procName.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写游戏名称和进程名称");
        return;
    }

    if (DataManager::instance().addGame(gameName, procName)) {
        m_gameNameEdit->clear();
        m_processNameEdit->clear();
        loadGameList();
        emit gameListChanged();
        m_statusLabel->setStyleSheet("color: green;");
        m_statusLabel->setText(QString("✅ 已添加：%1 (%2)").arg(gameName, procName));
    } else {
        QMessageBox::warning(this, "错误", "添加失败，可能进程名已存在");
    }
}

void GameListWidget::onRemoveGame()
{
    int row = m_gameTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的游戏");
        return;
    }

    QString gameName = m_gameTable->item(row, 0)->text();
    QString procName = m_gameTable->item(row, 1)->text();

    auto result = QMessageBox::question(this, "确认删除",
        QString("确定要从监控列表中移除「%1」吗？\n历史数据不会被删除。").arg(gameName),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        DataManager::instance().removeGame(procName);
        loadGameList();
        emit gameListChanged();
        m_statusLabel->setStyleSheet("color: #666;");
        m_statusLabel->setText(QString("已移除：%1").arg(gameName));
    }
}

void GameListWidget::onCellSelected(int row, int col)
{
    Q_UNUSED(row);
    Q_UNUSED(col);
}

void GameListWidget::refresh()
{
    loadGameList();
}
