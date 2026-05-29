#include "authmanager.h"
#include "datamanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
{
    // 恢复本地登录状态
    auto info = DataManager::instance().getUserInfo();
    if (info.isLoggedIn) {
        m_token = info.token;
        m_username = info.username;
        m_nickname = info.nickname;
    }
}

QString AuthManager::apiUrl(const QString& path) const
{
    return DataManager::instance().apiBaseUrl() + path;
}

bool AuthManager::isLoggedIn() const
{
    return !m_token.isEmpty();
}

QString AuthManager::currentUsername() const { return m_username; }
QString AuthManager::currentNickname() const { return m_nickname; }
QString AuthManager::token() const { return m_token; }

void AuthManager::registerUser(const QString& username, const QString& password,
                                const QString& nickname)
{
    QUrl url(apiUrl("/auth/register"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["username"] = username;
    body["password"] = password;
    if (!nickname.isEmpty())
        body["nickname"] = nickname;

    QNetworkReply* reply = m_network.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onRegisterReply(reply);
    });
}

void AuthManager::login(const QString& username, const QString& password)
{
    QUrl url(apiUrl("/auth/login"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["username"] = username;
    body["password"] = password;

    QNetworkReply* reply = m_network.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLoginReply(reply);
    });
}

void AuthManager::logout()
{
    m_token.clear();
    m_username.clear();
    m_nickname.clear();
    DataManager::instance().clearUserInfo();
    emit loggedOut();
}

void AuthManager::onLoginReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // 网络错误或服务器返回错误
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QString errMsg = "网络连接失败";

        if (doc.isObject() && doc.object().contains("message"))
            errMsg = doc.object()["message"].toString();
        else if (!data.isEmpty())
            errMsg = QString::fromUtf8(data);

        emit loginFailed(errMsg);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    if (obj.contains("token")) {
        m_token = obj["token"].toString();
        m_username = obj["username"].toString();
        m_nickname = obj["nickname"].toString();

        // 保存到本地
        UserInfo info;
        info.username = m_username;
        info.token = m_token;
        info.nickname = m_nickname;
        info.isLoggedIn = true;
        DataManager::instance().saveUserInfo(info);

        emit loginSuccess(m_username, m_nickname);
    } else {
        emit loginFailed("服务器返回数据异常");
    }
}

void AuthManager::onRegisterReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QString errMsg = "注册失败：网络连接失败";

        if (doc.isObject() && doc.object().contains("message"))
            errMsg = doc.object()["message"].toString();

        emit registerFailed(errMsg);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    if (obj.contains("username")) {
        emit registerSuccess(obj["username"].toString());
    } else {
        emit registerFailed("注册成功但服务器返回数据异常，请尝试登录");
    }
}
