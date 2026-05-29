#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QCheckBox>

class AuthManager;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(AuthManager* authManager, QWidget *parent = nullptr);

signals:
    void offlineModeRequested();

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onOfflineClicked();
    void switchToLogin();
    void switchToRegister();
    void onLoginSuccess(const QString& username, const QString& nickname);
    void onLoginFailed(const QString& error);
    void onRegisterSuccess(const QString& username);
    void onRegisterFailed(const QString& error);

private:
    void setupLoginPage();
    void setupRegisterPage();
    void setFormEnabled(bool enabled);

    AuthManager* m_auth;

    QStackedWidget* m_stackedWidget;

    // 登录页
    QLineEdit* m_loginUser;
    QLineEdit* m_loginPass;
    QCheckBox* m_rememberMe;
    QPushButton* m_loginBtn;
    QLabel* m_loginStatus;

    // 注册页
    QLineEdit* m_regUser;
    QLineEdit* m_regPass;
    QLineEdit* m_regPassConfirm;
    QLineEdit* m_regNickname;
    QPushButton* m_regBtn;
    QLabel* m_regStatus;
};

#endif // LOGINDIALOG_H
