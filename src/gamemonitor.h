#ifndef GAMEMONITOR_H
#define GAMEMONITOR_H

#include <QObject>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <QDateTime>

// 运行中的游戏会话
struct ActiveSession {
    int recordId = -1;
    QString gameName;
    QString processName;
    QDateTime startTime;
};

class GameMonitor : public QObject
{
    Q_OBJECT

public:
    explicit GameMonitor(QObject *parent = nullptr);
    ~GameMonitor();

    // 控制监控
    void start(int intervalMs = 5000);
    void stop();
    bool isRunning() const;

    // 获取当前正在运行的游戏
    QList<ActiveSession> activeSessions() const;

signals:
    // 游戏开始运行 (processName 被检测到)
    void gameStarted(const QString& gameName, const QString& processName);

    // 游戏停止运行 (进程已退出)
    void gameStopped(const QString& gameName, int durationSeconds);

    // 每 tick 发送当前活跃游戏信息
    void statusUpdated(const QList<ActiveSession>& sessions);

private slots:
    void pollProcesses();

private:
    // 检查指定进程是否在运行 (Windows API)
    bool isProcessRunning(const QString& processName) const;

    QTimer m_timer;
    // 当前活跃会话：processName -> ActiveSession
    QMap<QString, ActiveSession> m_activeSessions;
    // 所有监控的进程名集合
    QSet<QString> m_monitoredProcesses;
};

#endif // GAMEMONITOR_H
