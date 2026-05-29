#include "logindialog.h"
#include "authmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFont>

LoginDialog::LoginDialog(AuthManager* authManager, QWidget *parent)
    : QDialog(parent), m_auth(authManager)
{
    setWindowTitle("游戏时间统计 - 账号登录");
    setMinimumSize(380, 420);
    setMaximumSize(420, 580);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(24, 16, 24, 16);

    // 标题
    auto* titleLabel = new QLabel("🎮 游戏时间助手");
    QFont titleFont;
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("登录后可同步数据到云端");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet("color: #666; margin-bottom: 4px;");
    mainLayout->addWidget(subtitleLabel);

    // 切换页
    m_stackedWidget = new QStackedWidget;
    mainLayout->addWidget(m_stackedWidget);

    setupLoginPage();
    setupRegisterPage();

    // 页面切换链接
    auto* switchLayout = new QHBoxLayout;
    auto* toRegisterLink = new QPushButton("没有账号？点击注册");
    toRegisterLink->setFlat(true);
    toRegisterLink->setStyleSheet("color: #2196F3; border: none; text-decoration: underline;");
    connect(toRegisterLink, &QPushButton::clicked, this, &LoginDialog::switchToRegister);

    auto* toLoginLink = new QPushButton("已有账号？返回登录");
    toLoginLink->setFlat(true);
    toLoginLink->setStyleSheet("color: #2196F3; border: none; text-decoration: underline;");
    connect(toLoginLink, &QPushButton::clicked, this, &LoginDialog::switchToLogin);

    switchLayout->addWidget(toRegisterLink);
    switchLayout->addWidget(toLoginLink);
    mainLayout->addLayout(switchLayout);

    // 分隔
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #E0E0E0;");
    mainLayout->addWidget(sep);

    // 离线使用按钮
    auto* hintLabel = new QLabel("无需账号也能使用全部本地功能：");
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setStyleSheet("color: #888; font-size: 12px;");
    mainLayout->addWidget(hintLabel);

    auto* offlineBtn = new QPushButton("💻 离线使用（跳过登录）");
    offlineBtn->setStyleSheet(
        "QPushButton { background-color: #607D8B; color: white; padding: 10px; "
        "border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #455A64; }");
    connect(offlineBtn, &QPushButton::clicked, this, &LoginDialog::onOfflineClicked);
    mainLayout->addWidget(offlineBtn);

    // 连接信号
    connect(m_auth, &AuthManager::loginSuccess, this, &LoginDialog::onLoginSuccess);
    connect(m_auth, &AuthManager::loginFailed, this, &LoginDialog::onLoginFailed);
    connect(m_auth, &AuthManager::registerSuccess, this, &LoginDialog::onRegisterSuccess);
    connect(m_auth, &AuthManager::registerFailed, this, &LoginDialog::onRegisterFailed);

    m_stackedWidget->setCurrentIndex(0);
}

void LoginDialog::setupLoginPage()
{
    auto* page = new QWidget;
    auto* formLayout = new QFormLayout(page);
    formLayout->setSpacing(12);
    formLayout->setContentsMargins(8, 8, 8, 8);

    m_loginUser = new QLineEdit;
    m_loginUser->setPlaceholderText("请输入用户名");
    formLayout->addRow("用户名:", m_loginUser);

    m_loginPass = new QLineEdit;
    m_loginPass->setPlaceholderText("请输入密码");
    m_loginPass->setEchoMode(QLineEdit::Password);
    formLayout->addRow("密  码:", m_loginPass);

    m_rememberMe = new QCheckBox("记住登录状态");
    formLayout->addRow("", m_rememberMe);

    m_loginBtn = new QPushButton("登  录");
    m_loginBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; padding: 10px; "
        "border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #45a049; }");
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    formLayout->addRow("", m_loginBtn);

    m_loginStatus = new QLabel;
    m_loginStatus->setAlignment(Qt::AlignCenter);
    m_loginStatus->setWordWrap(true);
    formLayout->addRow("", m_loginStatus);

    m_stackedWidget->addWidget(page);
}

void LoginDialog::setupRegisterPage()
{
    auto* page = new QWidget;
    auto* formLayout = new QFormLayout(page);
    formLayout->setSpacing(12);
    formLayout->setContentsMargins(8, 8, 8, 8);

    m_regUser = new QLineEdit;
    m_regUser->setPlaceholderText("3-20个字符");
    formLayout->addRow("用户名:", m_regUser);

    m_regNickname = new QLineEdit;
    m_regNickname->setPlaceholderText("显示名称（选填）");
    formLayout->addRow("昵  称:", m_regNickname);

    m_regPass = new QLineEdit;
    m_regPass->setPlaceholderText("至少6个字符");
    m_regPass->setEchoMode(QLineEdit::Password);
    formLayout->addRow("密  码:", m_regPass);

    m_regPassConfirm = new QLineEdit;
    m_regPassConfirm->setPlaceholderText("再次输入密码");
    m_regPassConfirm->setEchoMode(QLineEdit::Password);
    formLayout->addRow("确认密码:", m_regPassConfirm);

    m_regBtn = new QPushButton("注  册");
    m_regBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; padding: 10px; "
        "border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #1976D2; }");
    connect(m_regBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    formLayout->addRow("", m_regBtn);

    m_regStatus = new QLabel;
    m_regStatus->setAlignment(Qt::AlignCenter);
    m_regStatus->setWordWrap(true);
    formLayout->addRow("", m_regStatus);

    m_stackedWidget->addWidget(page);
}

void LoginDialog::switchToLogin()
{
    m_stackedWidget->setCurrentIndex(0);
    m_loginStatus->clear();
}

void LoginDialog::switchToRegister()
{
    m_stackedWidget->setCurrentIndex(1);
    m_regStatus->clear();
}

void LoginDialog::setFormEnabled(bool enabled)
{
    m_loginBtn->setEnabled(enabled);
    m_loginUser->setEnabled(enabled);
    m_loginPass->setEnabled(enabled);
    m_regBtn->setEnabled(enabled);
    m_regUser->setEnabled(enabled);
    m_regPass->setEnabled(enabled);
    m_regPassConfirm->setEnabled(enabled);
    m_regNickname->setEnabled(enabled);
}

void LoginDialog::onLoginClicked()
{
    QString user = m_loginUser->text().trimmed();
    QString pass = m_loginPass->text();

    if (user.isEmpty() || pass.isEmpty()) {
        m_loginStatus->setStyleSheet("color: red;");
        m_loginStatus->setText("请输入用户名和密码");
        return;
    }

    m_loginStatus->setStyleSheet("color: #666;");
    m_loginStatus->setText("正在登录...");
    setFormEnabled(false);
    m_auth->login(user, pass);
}

void LoginDialog::onRegisterClicked()
{
    QString user = m_regUser->text().trimmed();
    QString pass = m_regPass->text();
    QString passConfirm = m_regPassConfirm->text();
    QString nickname = m_regNickname->text().trimmed();

    if (user.isEmpty() || pass.isEmpty()) {
        m_regStatus->setStyleSheet("color: red;");
        m_regStatus->setText("请填写用户名和密码");
        return;
    }

    if (user.length() < 3 || user.length() > 20) {
        m_regStatus->setStyleSheet("color: red;");
        m_regStatus->setText("用户名长度需要3-20个字符");
        return;
    }

    if (pass.length() < 6) {
        m_regStatus->setStyleSheet("color: red;");
        m_regStatus->setText("密码至少需要6个字符");
        return;
    }

    if (pass != passConfirm) {
        m_regStatus->setStyleSheet("color: red;");
        m_regStatus->setText("两次输入的密码不一致");
        return;
    }

    m_regStatus->setStyleSheet("color: #666;");
    m_regStatus->setText("正在注册...");
    setFormEnabled(false);
    m_auth->registerUser(user, pass, nickname);
}

void LoginDialog::onOfflineClicked()
{
    emit offlineModeRequested();
    accept();
}

void LoginDialog::onLoginSuccess(const QString& username, const QString& nickname)
{
    Q_UNUSED(username);
    QMessageBox::information(this, "登录成功",
        QString("欢迎回来，%1！").arg(nickname.isEmpty() ? username : nickname));
    accept();
}

void LoginDialog::onLoginFailed(const QString& error)
{
    m_loginStatus->setStyleSheet("color: red;");
    m_loginStatus->setText(error);
    setFormEnabled(true);
}

void LoginDialog::onRegisterSuccess(const QString& username)
{
    Q_UNUSED(username);
    m_regStatus->setStyleSheet("color: green;");
    m_regStatus->setText("注册成功！请切换到登录页面进行登录。");
    setFormEnabled(true);
}

void LoginDialog::onRegisterFailed(const QString& error)
{
    m_regStatus->setStyleSheet("color: red;");
    m_regStatus->setText(error);
    setFormEnabled(true);
}
