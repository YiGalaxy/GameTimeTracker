#include "syncmanager.h"
#include "datamanager.h"
#include "authmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

SyncManager::SyncManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_syncTimer, &QTimer::timeout, this, &SyncManager::doSync);
}

QString SyncManager::apiUrl(const QString& path) const
{
    return DataManager::instance().apiBaseUrl() + path;
}

QString SyncManager::authHeader() const
{
    return "Bearer " + DataManager::instance().getUserInfo().token;
}

void SyncManager::setAutoSyncInterval(int intervalMs)
{
    if (intervalMs > 0)
        m_syncTimer.start(intervalMs);
    else
        m_syncTimer.stop();
}

void SyncManager::syncNow()
{
    doSync();
}

bool SyncManager::isSyncing() const
{
    return m_syncing;
}

// ---- 全量同步（定时轮询） ----

void SyncManager::doSync()
{
    if (m_syncing) return;

    auto userInfo = DataManager::instance().getUserInfo();
    if (!userInfo.isLoggedIn) {
        qInfo() << "[sync] not logged in, skip";
        return;
    }

    m_syncing = true;
    emit syncStarted();
    uploadRecords();
}

void SyncManager::uploadRecords()
{
    auto records = DataManager::instance().getUnsyncedRecords();
    if (records.isEmpty()) {
        qInfo() << "[sync] no pending records to upload";
        downloadRecords(0);
        return;
    }

    QJsonArray arr;
    for (const auto& r : records) {
        QJsonObject obj;
        obj["game_name"]        = r.gameName;
        obj["process_name"]     = r.processName;
        obj["start_time"]       = r.startTime.toString(Qt::ISODate);
        obj["end_time"]         = r.endTime.toString(Qt::ISODate);
        obj["duration_seconds"] = r.durationSeconds;
        obj["date"]             = r.date.toString(Qt::ISODate);
        arr.append(obj);
    }

    QJsonObject body;
    body["records"] = arr;

    QUrl url(apiUrl("/sync/upload"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", authHeader().toUtf8());

    QNetworkReply* reply = m_network.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, records]() {
        reply->deleteLater();

        int uploaded = 0;
        if (reply->error() == QNetworkReply::NoError) {
            for (const auto& r : records) {
                DataManager::instance().markSynced(r.id);
                uploaded++;
            }
            qInfo() << "[sync] uploaded:" << uploaded << "records";
        } else {
            qWarning() << "[sync] upload failed:" << reply->errorString();
            emit syncFailed("Upload failed: " + reply->errorString());
            m_syncing = false;
            return;
        }

        downloadRecords(uploaded);
    });
}

void SyncManager::downloadRecords(int uploadedCount)
{
    QUrl url(apiUrl("/sync/download"));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", authHeader().toUtf8());

    QNetworkReply* reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, uploadedCount]() {
        reply->deleteLater();
        int downloaded = 0;

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);

            if (doc.isObject() && doc.object().contains("records")) {
                QJsonArray arr = doc.object()["records"].toArray();
                for (const auto& val : arr) {
                    QJsonObject obj = val.toObject();
                    GameRecord r;
                    r.gameName       = obj["game_name"].toString();
                    r.processName    = obj["process_name"].toString();
                    r.startTime      = QDateTime::fromString(obj["start_time"].toString(), Qt::ISODate);
                    r.endTime        = QDateTime::fromString(obj["end_time"].toString(), Qt::ISODate);
                    r.durationSeconds = obj["duration_seconds"].toInt();
                    r.date           = QDate::fromString(obj["date"].toString(), Qt::ISODate);
                    r.synced         = true;

                    int id = DataManager::instance().addGameRecord(r);
                    if (id > 0) {
                        DataManager::instance().updateGameRecordEnd(id, r.endTime);
                        DataManager::instance().markSynced(id);
                        downloaded++;
                    }
                }
            }
            qInfo() << "[sync] downloaded:" << downloaded << "records";
        } else {
            qWarning() << "[sync] download failed:" << reply->errorString();
            emit syncFailed("Download failed: " + reply->errorString());
            m_syncing = false;
            return;
        }

        m_syncing = false;
        emit syncCompleted(uploadedCount, downloaded);
    });
}

// ---- 实时事件同步 ----

void SyncManager::syncGameEvent(const QString& gameName, const QString& processName,
                                 const QString& eventType,
                                 const QDateTime& eventTime, int durationSeconds)
{
    auto userInfo = DataManager::instance().getUserInfo();
    if (!userInfo.isLoggedIn) {
        qInfo() << "[sync-event] not logged in, skip event";
        return;
    }

    QJsonObject body;
    body["game_name"]        = gameName;
    body["process_name"]     = processName;
    body["event_type"]       = eventType;
    body["event_time"]       = eventTime.toString(Qt::ISODate);
    body["duration_seconds"] = durationSeconds;
    body["date"]             = QDate::currentDate().toString(Qt::ISODate);

    // If it's a stop event, this is a complete record; add start_time too
    if (eventType == "stop") {
        body["start_time"] = eventTime.addSecs(-durationSeconds).toString(Qt::ISODate);
        body["end_time"]   = eventTime.toString(Qt::ISODate);
    }

    QUrl url(apiUrl("/sync/event"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", authHeader().toUtf8());

    QNetworkReply* reply = m_network.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameName]() {
        reply->deleteLater();

        bool success = (reply->error() == QNetworkReply::NoError);
        if (success) {
            qInfo() << "[sync-event] pushed:" << gameName;
        } else {
            qWarning() << "[sync-event] failed:" << gameName << reply->errorString();
        }
        emit gameEventSynced(gameName, success);
    });
}

void SyncManager::sendHeartbeat(const QJsonObject& activeGames)
{
    auto userInfo = DataManager::instance().getUserInfo();
    if (!userInfo.isLoggedIn) return;

    QUrl url(apiUrl("/sync/heartbeat"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", authHeader().toUtf8());

    QNetworkReply* reply = m_network.post(req, QJsonDocument(activeGames).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        bool success = (reply->error() == QNetworkReply::NoError);
        emit heartbeatSent(success);
    });
}
