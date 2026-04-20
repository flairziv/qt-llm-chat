#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QFile>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "ElaApplication.h"
#include "ui/MainWindow.h"
#include "core/AppSettings.h"

#ifdef Q_OS_WIN
// 全局热键 ID
static const int HOTKEY_ID = 0x1234;

/**
 * @brief 监听 Win32 WM_HOTKEY 事件，唤醒主窗口
 */
class GlobalHotkeyFilter : public QAbstractNativeEventFilter
{
public:
    GlobalHotkeyFilter(MainWindow *window) : m_window(window) {}
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override
    {
        Q_UNUSED(eventType)
        Q_UNUSED(result)
        MSG *msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == HOTKEY_ID) {
            if (m_window->isVisible() && m_window->isActiveWindow()) {
                m_window->hide();
            } else {
                m_window->show();
                m_window->raise();
                m_window->activateWindow();
            }
            return true;
        }
        return false;
    }
private:
    MainWindow *m_window;
};
#endif

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
