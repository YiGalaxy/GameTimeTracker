#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>

class AuthManager;
class SyncManager;
class GameMonitor;
class StatsWidget;
class GameListWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // 系统托盘
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showMainWindow();

    // 游戏状态
    void onGameStarted(const QString& gameName, const QString& processName);
    void onGameStopped(const QString& gameName, int durationSeconds);
    void updateActiveGamesDisplay();

    // 账户
    void onLoginAction();
    void onLogoutAction();
    void onOfflineMode();
    void onSettingsAction();
    void onLoginSuccess(const QString& username, const QString& nickname);
    void onLoggedOut();

    // 同步
    void onSyncCompleted(int uploaded, int downloaded);
    void onSyncFailed(const QString& error);

    // 页面切换
    void showOverviewPage();
    void showStatsPage();
    void showGameListPage();

private:
    void setupUI();
    void setupTrayIcon();
    void setupMenuBar();
    void createOverviewPage();
    void applyStylesheet();

    // 核心模块
    AuthManager*   m_auth;
    SyncManager*   m_sync;
    GameMonitor*   m_monitor;

    // UI 组件
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    QStackedWidget* m_centralStack;

    // 概览页
    QWidget* m_overviewPage;
    QLabel*  m_loginStatusLabel;
    QLabel*  m_activeGamesLabel;
    QLabel*  m_todayTimeLabel;
    QLabel*  m_syncStatusLabel;

    // 子页面
    StatsWidget*    m_statsWidget;
    GameListWidget* m_gameListWidget;

    // 状态刷新定时器
    QTimer* m_statusTimer;
};

#endif // MAINWINDOW_H
