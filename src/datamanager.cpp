#include "datamanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QVariant>

DataManager::DataManager() {}
DataManager::~DataManager() { close(); }

DataManager& DataManager::instance()
{
    static DataManager inst;
    return inst;
}

bool DataManager::initialize()
{
    // SQLite 数据库路径：AppData/Local/GameTimeTracker/data.db
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/data.db";

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCritical() << "无法打开数据库:" << m_db.lastError().text();
        return false;
    }

    qInfo() << "数据库已打开:" << dbPath;
    return createTables();
}

void DataManager::close()
{
    if (m_db.isOpen())
        m_db.close();
}

bool DataManager::createTables()
{
    QSqlQuery q(m_db);

    // 游戏列表配置表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS games (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            game_name TEXT NOT NULL,
            process_name TEXT NOT NULL UNIQUE,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )")) {
        qCritical() << "创建 games 表失败:" << q.lastError().text();
        return false;
    }

    // 游戏运行记录表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS game_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            game_name TEXT NOT NULL,
            process_name TEXT NOT NULL,
            start_time DATETIME NOT NULL,
            end_time DATETIME,
            duration_seconds INTEGER DEFAULT 0,
            date DATE NOT NULL,
            synced INTEGER DEFAULT 0
        )
    )")) {
        qCritical() << "创建 game_records 表失败:" << q.lastError().text();
        return false;
    }

    // 用户信息表（本地缓存）
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS user_info (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            username TEXT NOT NULL,
            token TEXT,
            nickname TEXT,
            logged_in INTEGER DEFAULT 0
        )
    )")) {
        qCritical() << "创建 user_info 表失败:" << q.lastError().text();
        return false;
    }

    // 设置表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )")) {
        qCritical() << "创建 settings 表失败:" << q.lastError().text();
        return false;
    }

    // 插入默认设置
    q.exec("INSERT OR IGNORE INTO settings(key, value) VALUES('api_base_url', 'http://localhost:8080/api')");
    q.exec("INSERT OR IGNORE INTO settings(key, value) VALUES('poll_interval_ms', '5000')");
    q.exec("INSERT OR IGNORE INTO settings(key, value) VALUES('auto_sync', 'true')");

    return true;
}

// ---- 游戏记录 ----

int DataManager::addGameRecord(const GameRecord& record)
{
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO game_records (game_name, process_name, start_time, date, synced)
        VALUES (?, ?, ?, ?, 0)
    )");
    q.addBindValue(record.gameName);
    q.addBindValue(record.processName);
    q.addBindValue(record.startTime.toString(Qt::ISODate));
    q.addBindValue(record.date.toString(Qt::ISODate));

    if (!q.exec()) {
        qWarning() << "添加游戏记录失败:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool DataManager::updateGameRecordEnd(int recordId, const QDateTime& endTime)
{
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE game_records
        SET end_time = ?,
            duration_seconds = strftime('%s', ?) - strftime('%s', start_time)
        WHERE id = ?
    )");
    q.addBindValue(endTime.toString(Qt::ISODate));
    q.addBindValue(endTime.toString(Qt::ISODate));
    q.addBindValue(recordId);

    if (!q.exec()) {
        qWarning() << "更新记录结束时间失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DataManager::markSynced(int recordId)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE game_records SET synced = 1 WHERE id = ?");
    q.addBindValue(recordId);
    return q.exec();
}

QList<GameRecord> DataManager::getRecordsByDate(const QDate& date)
{
    QList<GameRecord> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, game_name, process_name, start_time, end_time, duration_seconds, synced "
              "FROM game_records WHERE date = ? ORDER BY start_time DESC");
    q.addBindValue(date.toString(Qt::ISODate));

    if (q.exec()) {
        while (q.next()) {
            GameRecord r;
            r.id = q.value(0).toInt();
            r.gameName = q.value(1).toString();
            r.processName = q.value(2).toString();
            r.startTime = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
            r.endTime = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
            r.durationSeconds = q.value(5).toInt();
            r.synced = q.value(6).toBool();
            r.date = date;
            list.append(r);
        }
    }
    return list;
}

QList<GameRecord> DataManager::getUnsyncedRecords()
{
    QList<GameRecord> list;
    QSqlQuery q(m_db);
    q.exec("SELECT id, game_name, process_name, start_time, end_time, duration_seconds, date "
           "FROM game_records WHERE synced = 0 AND end_time IS NOT NULL");

    while (q.next()) {
        GameRecord r;
        r.id = q.value(0).toInt();
        r.gameName = q.value(1).toString();
        r.processName = q.value(2).toString();
        r.startTime = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        r.endTime = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
        r.durationSeconds = q.value(5).toInt();
        r.date = QDate::fromString(q.value(6).toString(), Qt::ISODate);
        r.synced = false;
        list.append(r);
    }
    return list;
}

// ---- 时间统计 ----

static QList<TimeStats> queryStats(DataManager*, const QString& sql, const QList<QVariant>& bindings)
{
    QMap<QString, TimeStats> map;
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(sql);
    for (const auto& b : bindings)
        q.addBindValue(b);

    if (q.exec()) {
        while (q.next()) {
            QString name = q.value(0).toString();
            TimeStats& s = map[name];
            s.gameName = name;
            s.totalSeconds = q.value(1).toInt();
            s.sessionCount = q.value(2).toInt();
        }
    }
    return map.values();
}

QList<TimeStats> DataManager::getDailyStats(const QDate& date)
{
    return queryStats(this,
        "SELECT game_name, COALESCE(SUM(duration_seconds), 0), COUNT(*) "
        "FROM game_records WHERE date = ? GROUP BY game_name "
        "ORDER BY SUM(duration_seconds) DESC",
        { date.toString(Qt::ISODate) });
}

QList<TimeStats> DataManager::getWeeklyStats(const QDate& startOfWeek)
{
    QDate endOfWeek = startOfWeek.addDays(6);
    return queryStats(this,
        "SELECT game_name, COALESCE(SUM(duration_seconds), 0), COUNT(*) "
        "FROM game_records WHERE date BETWEEN ? AND ? GROUP BY game_name "
        "ORDER BY SUM(duration_seconds) DESC",
        { startOfWeek.toString(Qt::ISODate), endOfWeek.toString(Qt::ISODate) });
}

QList<TimeStats> DataManager::getMonthlyStats(int year, int month)
{
    return queryStats(this,
        "SELECT game_name, COALESCE(SUM(duration_seconds), 0), COUNT(*) "
        "FROM game_records WHERE strftime('%Y', date) = ? AND strftime('%m', date) = ? "
        "GROUP BY game_name ORDER BY SUM(duration_seconds) DESC",
        { QString::number(year), QString("%1").arg(month, 2, 10, QChar('0')) });
}

QList<TimeStats> DataManager::getStatsByRange(const QDate& from, const QDate& to)
{
    return queryStats(this,
        "SELECT game_name, COALESCE(SUM(duration_seconds), 0), COUNT(*) "
        "FROM game_records WHERE date BETWEEN ? AND ? GROUP BY game_name "
        "ORDER BY SUM(duration_seconds) DESC",
        { from.toString(Qt::ISODate), to.toString(Qt::ISODate) });
}

int DataManager::getTotalPlayTimeToday()
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COALESCE(SUM(duration_seconds), 0) FROM game_records WHERE date = ?");
    q.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

// ---- 游戏列表 ----

bool DataManager::addGame(const QString& gameName, const QString& processName)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO games (game_name, process_name) VALUES (?, ?)");
    q.addBindValue(gameName);
    q.addBindValue(processName);
    if (!q.exec()) {
        qWarning() << "添加游戏失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DataManager::removeGame(const QString& processName)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM games WHERE process_name = ?");
    q.addBindValue(processName);
    return q.exec();
}

QList<QPair<QString, QString>> DataManager::getGameList()
{
    QList<QPair<QString, QString>> list;
    QSqlQuery q(m_db);
    q.exec("SELECT game_name, process_name FROM games ORDER BY game_name");
    while (q.next())
        list.append({ q.value(0).toString(), q.value(1).toString() });
    return list;
}

// ---- 用户认证 ----

bool DataManager::saveUserInfo(const UserInfo& info)
{
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT OR REPLACE INTO user_info (id, username, token, nickname, logged_in)
        VALUES (1, ?, ?, ?, ?)
    )");
    q.addBindValue(info.username);
    q.addBindValue(info.token);
    q.addBindValue(info.nickname);
    q.addBindValue(info.isLoggedIn ? 1 : 0);
    return q.exec();
}

UserInfo DataManager::getUserInfo()
{
    UserInfo info;
    QSqlQuery q(m_db);
    q.exec("SELECT username, token, nickname, logged_in FROM user_info WHERE id = 1");
    if (q.next()) {
        info.username = q.value(0).toString();
        info.token = q.value(1).toString();
        info.nickname = q.value(2).toString();
        info.isLoggedIn = q.value(3).toBool();
    }
    return info;
}

bool DataManager::clearUserInfo()
{
    QSqlQuery q(m_db);
    return q.exec("DELETE FROM user_info");
}

// ---- 设置 ----

QString DataManager::getSetting(const QString& key, const QString& defaultValue) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM settings WHERE key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}

bool DataManager::setSetting(const QString& key, const QString& value)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QString DataManager::apiBaseUrl() const
{
    return getSetting("api_base_url", "http://47.109.182.255:8080/api");
}
