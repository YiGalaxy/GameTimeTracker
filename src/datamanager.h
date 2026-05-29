#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include <QList>
#include <QString>

// 游戏记录数据结构
struct GameRecord {
    int id = 0;
    QString gameName;
    QString processName;   // 进程名，如 "r5apex.exe"
    QDateTime startTime;
    QDateTime endTime;
    int durationSeconds = 0; // 本次运行秒数
    bool synced = false;     // 是否已同步到云端

    // 查询用
    QDate date;  // 所属日期
};

// 每日/每周/每月统计
struct TimeStats {
    QString gameName;
    int totalSeconds = 0;
    int sessionCount = 0;

    int hours()   const { return totalSeconds / 3600; }
    int minutes() const { return (totalSeconds % 3600) / 60; }
    QString durationStr() const {
        return QString("%1小时%2分钟").arg(hours()).arg(minutes());
    }
};

// 用户账号信息（本地缓存）
struct UserInfo {
    int localId = 0;
    QString username;
    QString token;       // JWT token
    QString nickname;
    bool isLoggedIn = false;
};

class DataManager : public QObject
{
    Q_OBJECT

public:
    static DataManager& instance();

    bool initialize();
    void close();

    // ---- 游戏记录 CRUD ----
    int  addGameRecord(const GameRecord& record);
    bool updateGameRecordEnd(int recordId, const QDateTime& endTime);
    bool markSynced(int recordId);
    QList<GameRecord> getRecordsByDate(const QDate& date);
    QList<GameRecord> getUnsyncedRecords();

    // ---- 时间统计查询 ----
    QList<TimeStats> getDailyStats(const QDate& date);
    QList<TimeStats> getWeeklyStats(const QDate& startOfWeek);
    QList<TimeStats> getMonthlyStats(int year, int month);
    QList<TimeStats> getStatsByRange(const QDate& from, const QDate& to);
    int getTotalPlayTimeToday();

    // ---- 游戏列表管理 ----
    bool addGame(const QString& gameName, const QString& processName);
    bool removeGame(const QString& processName);
    QList<QPair<QString, QString>> getGameList(); // (gameName, processName)

    // ---- 用户认证 ----
    bool saveUserInfo(const UserInfo& info);
    UserInfo getUserInfo();
    bool clearUserInfo();

    // ---- 设置 ----
    QString getSetting(const QString& key, const QString& defaultValue = "") const;
    bool setSetting(const QString& key, const QString& value);

    // 同步 API 地址
    QString apiBaseUrl() const;

private:
    DataManager();
    ~DataManager();
    DataManager(const DataManager&) = delete;
    DataManager& operator=(const DataManager&) = delete;

    bool createTables();
    QSqlDatabase m_db;
};

#endif // DATAMANAGER_H
