#include <QApplication>
#include <QMessageBox>
#include "mainwindow.h"
#include "datamanager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("GameTimeTracker");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("GameTimeTracker");
    app.setQuitOnLastWindowClosed(false); // 关闭窗口时隐藏到托盘

    // 初始化数据库
    if (!DataManager::instance().initialize()) {
        QMessageBox::critical(nullptr, "错误",
            "无法初始化数据库，程序将退出。");
        return 1;
    }

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
