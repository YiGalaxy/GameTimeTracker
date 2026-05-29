#include "gamemonitor.h"
#include "datamanager.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

GameMonitor::GameMonitor(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &GameMonitor::pollProcesses);
}

GameMonitor::~GameMonitor()
{
    stop();
}

void GameMonitor::start(int intervalMs)
{
    // 加载游戏列表
    auto games = DataManager::instance().getGameList();
    m_monitoredProcesses.clear();
    for (const auto& g : games)
        m_monitoredProcesses.insert(g.second.toLower());

    qInfo() << "开始监控" << m_monitoredProcesses.size() << "个游戏进程";

    m_timer.start(intervalMs);
}

void GameMonitor::stop()
{
    m_timer.stop();

    // 结束所有活跃会话
    auto now = QDateTime::currentDateTime();
    for (auto it = m_activeSessions.begin(); it != m_activeSessions.end(); ++it) {
        DataManager::instance().updateGameRecordEnd(it.value().recordId, now);
        int duration = it.value().startTime.secsTo(now);
        emit gameStopped(it.value().gameName, duration);
    }
    m_activeSessions.clear();
}

bool GameMonitor::isRunning() const
{
    return m_timer.isActive();
}

QList<ActiveSession> GameMonitor::activeSessions() const
{
    return m_activeSessions.values();
}

bool GameMonitor::isProcessRunning(const QString& processName) const
{
#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    bool found = false;
    if (Process32FirstW(snapshot, &pe)) {
        do {
            QString name = QString::fromWCharArray(pe.szExeFile).toLower();
            if (name == processName.toLower()) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return found;
#else
    Q_UNUSED(processName);
    return false;
#endif
}

void GameMonitor::pollProcesses()
{
    auto now = QDateTime::currentDateTime();
    QDate today = QDate::currentDate();

    // 重新加载游戏列表（可能用户修改了）
    auto games = DataManager::instance().getGameList();
    QSet<QString> currentProcesses;
    for (const auto& g : games)
        currentProcesses.insert(g.second.toLower());
    m_monitoredProcesses = currentProcesses;

    // 1. 检查已监控的进程：哪些还在运行，哪些已退出
    QSet<QString> toRemove;
    for (auto it = m_activeSessions.begin(); it != m_activeSessions.end(); ++it) {
        if (!isProcessRunning(it.key())) {
            // 进程已退出
            DataManager::instance().updateGameRecordEnd(it.value().recordId, now);
            int duration = it.value().startTime.secsTo(now);
            qInfo() << "游戏结束:" << it.value().gameName
                     << "运行时长:" << duration << "秒";
            emit gameStopped(it.value().gameName, duration);
            toRemove.insert(it.key());
        }
    }
    for (const auto& key : toRemove)
        m_activeSessions.remove(key);

    // 2. 检查是否有新进程启动
    for (const auto& g : games) {
        QString procName = g.second.toLower();
        if (m_activeSessions.contains(procName))
            continue; // 已在监控中

        if (isProcessRunning(procName)) {
            // 新的游戏会话
            GameRecord record;
            record.gameName = g.first;
            record.processName = procName;
            record.startTime = now;
            record.date = today;

            int recordId = DataManager::instance().addGameRecord(record);
            if (recordId > 0) {
                ActiveSession session;
                session.recordId = recordId;
                session.gameName = g.first;
                session.processName = procName;
                session.startTime = now;
                m_activeSessions[procName] = session;

                qInfo() << "游戏开始:" << g.first << "(" << procName << ")";
                emit gameStarted(g.first, procName);
            }
        }
    }

    // 3. 发送状态更新
    if (!m_activeSessions.isEmpty())
        emit statusUpdated(m_activeSessions.values());
}
