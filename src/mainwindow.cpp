#include "mainwindow.h"
#include "authmanager.h"
#include "syncmanager.h"
#include "gamemonitor.h"
#include "statswidget.h"
#include "gamelistwidget.h"
#include "logindialog.h"
#include "datamanager.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QMenuBar>
#include <QCloseEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QFont>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("🎮 游戏时间助手");
    setMinimumSize(500, 600);
    resize(520, 650);

    // 初始化核心模块
    m_auth    = new AuthManager(this);
    m_sync    = new SyncManager(this);
    m_monitor = new GameMonitor(this);

    // 连接信号
    connect(m_monitor, &GameMonitor::gameStarted, this, &MainWindow::onGameStarted);
    connect(m_monitor, &GameMonitor::gameStopped, this, &MainWindow::onGameStopped);
    connect(m_sync, &SyncManager::syncCompleted, this, &MainWindow::onSyncCompleted);
    connect(m_sync, &SyncManager::syncFailed, this, &MainWindow::onSyncFailed);
    connect(m_auth, &AuthManager::loginSuccess, this, &MainWindow::onLoginSuccess);
    connect(m_auth, &AuthManager::loggedOut, this, &MainWindow::onLoggedOut);

    setupUI();
    setupTrayIcon();
    setupMenuBar();
    applyStylesheet();

    // 启动游戏监控（默认 5 秒轮询）
    int interval = DataManager::instance().getSetting("poll_interval_ms", "5000").toInt();
    m_monitor->start(interval);

    // 自动同步（如果已登录 & 开启了自动同步）
    if (m_auth->isLoggedIn() &&
        DataManager::instance().getSetting("auto_sync", "true") == "true") {
        m_sync->setAutoSyncInterval(300000); // 5 分钟一次
        m_sync->syncNow();
    }

    // 状态刷新定时器
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateActiveGamesDisplay);
    m_statusTimer->start(3000); // 每 3 秒刷新 UI

    updateActiveGamesDisplay();
}

MainWindow::~MainWindow()
{
    m_monitor->stop();
}

void MainWindow::setupUI()
{
    m_centralStack = new QStackedWidget;
    setCentralWidget(m_centralStack);

    // 页面 0: 概览页
    createOverviewPage();
    m_centralStack->addWidget(m_overviewPage);

    // 页面 1: 统计页
    m_statsWidget = new StatsWidget;
    connect(m_statsWidget, &StatsWidget::closeRequested, this, &MainWindow::showOverviewPage);
    m_centralStack->addWidget(m_statsWidget);

    // 页面 2: 游戏列表页
    m_gameListWidget = new GameListWidget;
    connect(m_gameListWidget, &GameListWidget::closeRequested, this, &MainWindow::showOverviewPage);
    connect(m_gameListWidget, &GameListWidget::gameListChanged, [this]() {
        // 游戏列表变化时重启监控
        m_monitor->stop();
        int interval = DataManager::instance().getSetting("poll_interval_ms", "5000").toInt();
        m_monitor->start(interval);
    });
    m_centralStack->addWidget(m_gameListWidget);

    // 默认显示概览页
    m_centralStack->setCurrentIndex(0);
}

void MainWindow::createOverviewPage()
{
    m_overviewPage = new QWidget;
    auto* layout = new QVBoxLayout(m_overviewPage);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 12, 16, 12);

    // ---- 账号状态 ----
    auto* accountGroup = new QGroupBox("👤 账号状态");
    auto* accountLayout = new QVBoxLayout(accountGroup);

    m_loginStatusLabel = new QLabel("未登录");
    m_loginStatusLabel->setStyleSheet("color: #F44336; font-weight: bold;");
    accountLayout->addWidget(m_loginStatusLabel);

    auto* accountBtnLayout = new QHBoxLayout;
    auto* loginBtn = new QPushButton("登  录");
    loginBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; padding: 8px 24px; "
        "border-radius: 4px; } QPushButton:hover { background-color: #1976D2; }");
    connect(loginBtn, &QPushButton::clicked, this, &MainWindow::onLoginAction);
    accountBtnLayout->addWidget(loginBtn);

    auto* logoutBtn = new QPushButton("登  出");
    logoutBtn->setStyleSheet(
        "QPushButton { background-color: #FF9800; color: white; padding: 8px 24px; "
        "border-radius: 4px; } QPushButton:hover { background-color: #F57C00; }");
    connect(logoutBtn, &QPushButton::clicked, this, &MainWindow::onLogoutAction);
    accountBtnLayout->addWidget(logoutBtn);

    auto* syncBtn = new QPushButton("🔄 立即同步");
    connect(syncBtn, &QPushButton::clicked, [this]() { m_sync->syncNow(); });
    accountBtnLayout->addWidget(syncBtn);
    accountBtnLayout->addStretch();

    accountLayout->addLayout(accountBtnLayout);

    m_syncStatusLabel = new QLabel;
    m_syncStatusLabel->setStyleSheet("color: #666; font-size: 12px;");
    accountLayout->addWidget(m_syncStatusLabel);

    layout->addWidget(accountGroup);

    // ---- 当前运行 ----
    auto* activeGroup = new QGroupBox("🎯 当前游戏状态");
    auto* activeLayout = new QVBoxLayout(activeGroup);

    m_activeGamesLabel = new QLabel("没有检测到游戏运行");
    m_activeGamesLabel->setStyleSheet(
        "padding: 16px; background-color: #F5F5F5; border-radius: 4px; font-size: 13px;");
    m_activeGamesLabel->setWordWrap(true);
    activeLayout->addWidget(m_activeGamesLabel);

    layout->addWidget(activeGroup);

    // ---- 今日统计 ----
    auto* todayGroup = new QGroupBox("📊 今日数据");
    auto* todayLayout = new QVBoxLayout(todayGroup);

    m_todayTimeLabel = new QLabel("今日游戏时间：0小时0分钟");
    m_todayTimeLabel->setStyleSheet(
        "font-size: 20px; font-weight: bold; color: #4CAF50; padding: 12px;");
    m_todayTimeLabel->setAlignment(Qt::AlignCenter);
    todayLayout->addWidget(m_todayTimeLabel);

    layout->addWidget(todayGroup);

    // ---- 快捷操作 ----
    auto* actionGroup = new QGroupBox("⚡ 快捷操作");
    auto* actionLayout = new QHBoxLayout(actionGroup);

    auto* statsBtn = new QPushButton("📊 详细统计");
    statsBtn->setStyleSheet(
        "QPushButton { padding: 12px 20px; border-radius: 4px; "
        "background-color: #E3F2FD; border: 1px solid #90CAF9; } "
        "QPushButton:hover { background-color: #BBDEFB; }");
    connect(statsBtn, &QPushButton::clicked, this, &MainWindow::showStatsPage);
    actionLayout->addWidget(statsBtn);

    auto* gameListBtn = new QPushButton("🎮 游戏管理");
    gameListBtn->setStyleSheet(
        "QPushButton { padding: 12px 20px; border-radius: 4px; "
        "background-color: #FFF3E0; border: 1px solid #FFCC80; } "
        "QPushButton:hover { background-color: #FFE0B2; }");
    connect(gameListBtn, &QPushButton::clicked, this, &MainWindow::showGameListPage);
    actionLayout->addWidget(gameListBtn);

    layout->addWidget(actionGroup);

    layout->addStretch();

    // 更新账号状态显示
    if (m_auth->isLoggedIn()) {
        onLoginSuccess(m_auth->currentUsername(), m_auth->currentNickname());
    } else {
        // 离线模式 - 显示友好状态
        m_loginStatusLabel->setText("💻 离线模式 — 本地数据");
        m_loginStatusLabel->setStyleSheet("color: #607D8B; font-weight: bold;");
    }
}

void MainWindow::setupTrayIcon()
{
    // 创建托盘图标（使用纯色方块作为图标，因为没有外部图标文件）
    QPixmap pixmap(16, 16);
    pixmap.fill(QColor("#4CAF50"));
    QIcon icon(pixmap);

    m_trayIcon = new QSystemTrayIcon(icon, this);
    m_trayIcon->setToolTip("游戏时间助手");

    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("显示主窗口", this, &MainWindow::showMainWindow);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("退出", qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_trayIcon->show();
}

void MainWindow::setupMenuBar()
{
    auto* menuBar = this->menuBar();

    auto* viewMenu = menuBar->addMenu("视图");
    viewMenu->addAction("概览", this, &MainWindow::showOverviewPage);
    viewMenu->addAction("统计", this, &MainWindow::showStatsPage);
    viewMenu->addAction("游戏管理", this, &MainWindow::showGameListPage);

    auto* accountMenu = menuBar->addMenu("账号");
    accountMenu->addAction("登录/注册", this, &MainWindow::onLoginAction);
    accountMenu->addAction("登出", this, &MainWindow::onLogoutAction);
    accountMenu->addSeparator();
    accountMenu->addAction("离线模式", this, &MainWindow::onOfflineMode);
    accountMenu->addSeparator();
    accountMenu->addAction("立即同步", [this]() { m_sync->syncNow(); });
    accountMenu->addSeparator();
    accountMenu->addAction("设置...", this, &MainWindow::onSettingsAction);

    auto* helpMenu = menuBar->addMenu("帮助");
    helpMenu->addAction("关于", [this]() {
        QMessageBox::about(this, "关于 游戏时间助手",
            "游戏时间助手 v1.0.0\n\n"
            "功能：\n"
            "• 自动监测游戏进程运行时间\n"
            "• 多维度时间统计分析\n"
            "• 账号登录与云端数据同步\n\n"
            "开发技术：Qt C++");
    });
}

void MainWindow::applyStylesheet()
{
    setStyleSheet(R"(
        QMainWindow { background-color: #FAFAFA; }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #E0E0E0;
            border-radius: 6px;
            margin-top: 8px;
            padding-top: 16px;
            background-color: white;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
    )");
}

// ---- 槽函数 ----

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 最小化到托盘，不退出
    if (m_trayIcon->isVisible()) {
        hide();
        m_trayIcon->showMessage("游戏时间助手", "程序已最小化到系统托盘，仍在后台监控中。",
                                 QSystemTrayIcon::Information, 2000);
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
        showMainWindow();
}

void MainWindow::showMainWindow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::onGameStarted(const QString& gameName, const QString& processName)
{
    Q_UNUSED(processName);
    m_trayIcon->showMessage("Game Started", QString("%1 is now running").arg(gameName),
                             QSystemTrayIcon::Information, 2000);

    // Real-time push to server
    m_sync->syncGameEvent(gameName, processName, "start",
                           QDateTime::currentDateTime(), 0);

    updateActiveGamesDisplay();
}

void MainWindow::onGameStopped(const QString& gameName, int durationSeconds)
{
    int h = durationSeconds / 3600;
    int m = (durationSeconds % 3600) / 60;
    m_trayIcon->showMessage("Game Stopped",
        QString("%1 - session: %2h %3m").arg(gameName).arg(h).arg(m),
        QSystemTrayIcon::Information, 3000);

    // Real-time push to server with duration
    m_sync->syncGameEvent(gameName, "", "stop",
                           QDateTime::currentDateTime(), durationSeconds);

    updateActiveGamesDisplay();
}

void MainWindow::updateActiveGamesDisplay()
{
    auto sessions = m_monitor->activeSessions();

    if (sessions.isEmpty()) {
        m_activeGamesLabel->setText("💤 没有检测到游戏运行\n\n"
            "添加游戏到监控列表后，当游戏运行时将自动统计时间。\n"
            "点击下方「游戏管理」添加要监控的游戏。");
        m_activeGamesLabel->setStyleSheet(
            "padding: 16px; background-color: #F5F5F5; border-radius: 4px; font-size: 13px; "
            "color: #888;");
    } else {
        QString text;
        for (const auto& s : sessions) {
            int elapsed = s.startTime.secsTo(QDateTime::currentDateTime());
            int h = elapsed / 3600;
            int m = (elapsed % 3600) / 60;
            int sec = elapsed % 60;
            text += QString("🟢 %1 — 已运行 %2:%3:%4\n")
                        .arg(s.gameName)
                        .arg(h, 2, 10, QChar('0'))
                        .arg(m, 2, 10, QChar('0'))
                        .arg(sec, 2, 10, QChar('0'));
        }
        m_activeGamesLabel->setText(text.trimmed());
        m_activeGamesLabel->setStyleSheet(
            "padding: 16px; background-color: #E8F5E9; border-radius: 4px; font-size: 14px; "
            "font-family: 'Consolas', monospace; color: #2E7D32;");
    }

    // 刷新今日时间
    int totalSecs = DataManager::instance().getTotalPlayTimeToday();
    // 加上当前活跃会话的实时时间
    for (const auto& s : sessions)
        totalSecs += s.startTime.secsTo(QDateTime::currentDateTime());

    int h = totalSecs / 3600;
    int m = (totalSecs % 3600) / 60;
    m_todayTimeLabel->setText(QString("今日游戏时间：%1小时%2分钟").arg(h).arg(m));

    // 发送心跳到服务器（推送当前活跃游戏状态）
    QJsonObject heartbeat;
    heartbeat["type"] = "heartbeat";
    if (!sessions.isEmpty()) {
        QJsonArray active;
        for (const auto& s : sessions) {
            QJsonObject g;
            g["game_name"]    = s.gameName;
            g["process_name"] = s.processName;
            g["start_time"]   = s.startTime.toString(Qt::ISODate);
            g["elapsed"]      = s.startTime.secsTo(QDateTime::currentDateTime());
            active.append(g);
        }
        heartbeat["active_games"] = active;
    } else {
        heartbeat["active_games"] = QJsonArray();
    }
    m_sync->sendHeartbeat(heartbeat);

    // 同时刷新统计页（如果可见）
    m_statsWidget->refresh();
}

void MainWindow::onLoginAction()
{
    LoginDialog dlg(m_auth, this);
    connect(&dlg, &LoginDialog::offlineModeRequested, this, &MainWindow::onOfflineMode);
    dlg.exec(); // 阻塞，直到登录成功/离线/取消
}

void MainWindow::onLogoutAction()
{
    auto result = QMessageBox::question(this, "确认登出",
        "确定要登出吗？\n本地数据不会丢失，将切换为离线模式。");
    if (result == QMessageBox::Yes)
        m_auth->logout();
}

void MainWindow::onOfflineMode()
{
    // 切换到离线模式（无需操作，因为已经是默认状态）
    m_loginStatusLabel->setText("💻 离线模式 — 本地数据");
    m_loginStatusLabel->setStyleSheet("color: #607D8B; font-weight: bold;");
    m_sync->setAutoSyncInterval(0);
    m_syncStatusLabel->setText("📴 离线模式，数据仅保存在本地");
    m_syncStatusLabel->setStyleSheet("color: #888; font-size: 12px;");
}

void MainWindow::onSettingsAction()
{
    // 简单设置对话框
    QStringList items;
    items << "API 服务器地址"
          << "进程检测间隔 (毫秒)"
          << "自动同步";

    bool ok;
    QString choice = QInputDialog::getItem(this, "设置", "选择要修改的配置项:", items, 0, false, &ok);
    if (!ok) return;

    int idx = items.indexOf(choice);
    if (idx == 0) {
        QString current = DataManager::instance().getSetting("api_base_url", "http://localhost:8080/api");
        QString val = QInputDialog::getText(this, "API 服务器地址",
            "请输入 API 服务器地址:", QLineEdit::Normal, current, &ok);
        if (ok && !val.isEmpty()) {
            DataManager::instance().setSetting("api_base_url", val);
            QMessageBox::information(this, "设置已保存", "API 地址已更新为:\n" + val);
        }
    } else if (idx == 1) {
        QString current = DataManager::instance().getSetting("poll_interval_ms", "5000");
        int val = QInputDialog::getInt(this, "进程检测间隔",
            "检测间隔 (毫秒，1000-60000):", current.toInt(), 1000, 60000, 1000, &ok);
        if (ok) {
            DataManager::instance().setSetting("poll_interval_ms", QString::number(val));
            m_monitor->stop();
            m_monitor->start(val);
            QMessageBox::information(this, "设置已保存",
                QString("检测间隔已更新为 %1 毫秒").arg(val));
        }
    } else if (idx == 2) {
        QString current = DataManager::instance().getSetting("auto_sync", "true");
        QString val = QInputDialog::getItem(this, "自动同步",
            "是否自动同步云端数据?", QStringList() << "true" << "false",
            current == "true" ? 0 : 1, false, &ok);
        if (ok) {
            DataManager::instance().setSetting("auto_sync", val);
            if (val == "true" && m_auth->isLoggedIn()) {
                m_sync->setAutoSyncInterval(300000);
            } else {
                m_sync->setAutoSyncInterval(0);
            }
        }
    }
}

void MainWindow::onLoginSuccess(const QString& username, const QString& nickname)
{
    QString displayName = nickname.isEmpty() ? username : nickname;
    m_loginStatusLabel->setText(QString("✅ 已登录：%1").arg(displayName));
    m_loginStatusLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");

    // 开启自动同步
    if (DataManager::instance().getSetting("auto_sync", "true") == "true") {
        m_sync->setAutoSyncInterval(300000);
        m_sync->syncNow();
    }
}

void MainWindow::onLoggedOut()
{
    m_loginStatusLabel->setText("💻 离线模式 — 本地数据");
    m_loginStatusLabel->setStyleSheet("color: #607D8B; font-weight: bold;");
    m_sync->setAutoSyncInterval(0);
    m_syncStatusLabel->setText("📴 离线模式，数据仅保存在本地");
    m_syncStatusLabel->setStyleSheet("color: #888; font-size: 12px;");
}

void MainWindow::onSyncCompleted(int uploaded, int downloaded)
{
    QString msg;
    if (uploaded > 0 || downloaded > 0)
        msg = QString("✅ 同步完成 — 上传 %1 条，下载 %2 条").arg(uploaded).arg(downloaded);
    else
        msg = "✅ 同步完成 — 数据已是最新";

    m_syncStatusLabel->setText(msg);
    m_syncStatusLabel->setStyleSheet("color: #4CAF50; font-size: 12px;");
    updateActiveGamesDisplay();
}

void MainWindow::onSyncFailed(const QString& error)
{
    m_syncStatusLabel->setText("❌ " + error);
    m_syncStatusLabel->setStyleSheet("color: #F44336; font-size: 12px;");
}

// ---- 页面切换 ----

void MainWindow::showOverviewPage()
{
    m_centralStack->setCurrentIndex(0);
    updateActiveGamesDisplay();
}

void MainWindow::showStatsPage()
{
    m_centralStack->setCurrentIndex(1);
    m_statsWidget->refresh();
}

void MainWindow::showGameListPage()
{
    m_centralStack->setCurrentIndex(2);
    m_gameListWidget->refresh();
}
