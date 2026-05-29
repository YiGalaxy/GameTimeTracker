#ifndef SYNCMANAGER_H
#define SYNCMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonObject>

class SyncManager : public QObject
{
    Q_OBJECT

public:
    explicit SyncManager(QObject *parent = nullptr);

    // 设置同步间隔（毫秒），0 表示不自动同步
    void setAutoSyncInterval(int intervalMs);

    // 手动触发全量同步（上传未同步记录 + 下载云端数据）
    void syncNow();

    // 实时事件同步：游戏开始/停止时立即推送单条记录
    void syncGameEvent(const QString& gameName, const QString& processName,
                       const QString& eventType,  // "start" or "stop"
                       const QDateTime& eventTime, int durationSeconds = 0);

    // 实时心跳：上报当前正在运行的游戏
    void sendHeartbeat(const QJsonObject& activeGames);

    // 是否正在同步
    bool isSyncing() const;

signals:
    void syncStarted();
    // uploaded: 上传条数, downloaded: 下载条数
    void syncCompleted(int uploaded, int downloaded);
    void syncFailed(const QString& error);

    // 实时事件推送结果
    void gameEventSynced(const QString& gameName, bool success);
    void heartbeatSent(bool success);

private slots:
    void doSync();

private:
    void uploadRecords();
    void downloadRecords(int uploadedCount = 0);

    QNetworkAccessManager m_network;
    QTimer m_syncTimer;
    bool m_syncing = false;

    QString apiUrl(const QString& path) const;
    QString authHeader() const;
};

#endif // SYNCMANAGER_H
