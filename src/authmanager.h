#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class AuthManager : public QObject
{
    Q_OBJECT

public:
    explicit AuthManager(QObject *parent = nullptr);

    // 注册
    void registerUser(const QString& username, const QString& password,
                      const QString& nickname = "");

    // 登录
    void login(const QString& username, const QString& password);

    // 登出
    void logout();

    // 检查本地登录状态
    bool isLoggedIn() const;
    QString currentUsername() const;
    QString currentNickname() const;
    QString token() const;

signals:
    void loginSuccess(const QString& username, const QString& nickname);
    void loginFailed(const QString& error);
    void registerSuccess(const QString& username);
    void registerFailed(const QString& error);
    void loggedOut();

private slots:
    void onLoginReply(QNetworkReply* reply);
    void onRegisterReply(QNetworkReply* reply);

private:
    QString apiUrl(const QString& path) const;
    QNetworkAccessManager m_network;
    QString m_token;
    QString m_username;
    QString m_nickname;
};

#endif // AUTHMANAGER_H
