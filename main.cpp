#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QFile>
#include "ElaApplication.h"
#include "ui/MainWindow.h"
#include "core/AppSettings.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("LLMChat");
    app.setOrganizationName("LLMChat");

    // 初始化 ElaWidgetTools（加载图标字体、主题等资源）
    eApp->init();

    // Load QSS stylesheet
    QFile styleFile(":/style/style.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }

    AppSettings settings;

    MainWindow mainWindow(&settings);

    QSystemTrayIcon sysTray(QIcon(":/imgs/tray.ico"), &mainWindow);
    QMenu menu;
    auto showAct = new QAction("show", &sysTray);
    auto exitAct = new QAction("exit", &sysTray);

    QObject::connect(showAct, &QAction::triggered, [&](){
        mainWindow.setVisible(true);
    });
    QObject::connect(exitAct, &QAction::triggered, [&](){
        QApplication::quit();
    });

    menu.addAction(showAct);
    menu.addAction(exitAct);

    sysTray.setContextMenu(&menu);

    // 点击托盘图标也显示/隐藏窗口
    QObject::connect(&sysTray, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason){
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (!mainWindow.isVisible()) {
                mainWindow.show();
                mainWindow.raise();
                mainWindow.activateWindow();
            } else {
                // 切换隐藏
                mainWindow.hide();
            }
        }
    });

    sysTray.show();


    mainWindow.show();

    return app.exec();
}
