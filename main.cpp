#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QFile>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>     // SetCurrentProcessExplicitAppUserModelID
#endif

#include "ElaApplication.h"
#include "ElaTheme.h"
#include "ui/MainWindow.h"
#include "core/AppSettings.h"
#include "core/ToolRegistry.h"
#include "core/tools/FileSystemTools.h"

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
    app.setOrganizationName("JulyJolly");

#ifdef Q_OS_WIN
    // 显式声明 AppUserModelID：Windows 用它把"任务栏固定的快捷方式"和"正在运行的
    // 进程"绑成一组。不设的话 Windows 会按 EXE 路径自动猜，路径变化（比如 build
    // 输出 vs 安装目录）就匹配不上，表现是固定后图标空白 / 任务栏显示两个分组。
    // 字符串可以随便取，但跨版本要保持一致。
    SetCurrentProcessExplicitAppUserModelID(L"Anthropic.LLMChat");
#endif

    // 初始化 ElaWidgetTools（加载图标字体、主题等资源）
    eApp->init();

    // Load QSS stylesheet (base — theme-independent layout/borders/backgrounds)
    QString baseQss;
    {
        QFile styleFile(":/style/style.qss");
        if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            baseQss = QString::fromUtf8(styleFile.readAll());
            styleFile.close();
        }
    }

    // 全局主题色同步：ElaWidgetTools 切换主题时只更新自己控件的 palette，
    // 标准 Qt 控件（QListWidget、QTextEdit、QLineEdit 的 placeholder、QLabel）依赖
    // QApplication 的 palette。这里显式同步关键文字色，让所有控件统一切换。
    //
    // 但仅靠 qApp->setPalette 不够 —— ElaWindow 内部会覆盖子控件的 palette，
    // 使标准 Qt 输入框 / QMenu / QComboBox 的文字色不跟随主题（夜间模式下黑字看不见，
    // 表现为 prompt 模板框 / spinbox 输入只能显示英文等亮色字符的"假象"）。
    // 所以同时把主题相关的 color/QMenu 配色作为 QSS 追加到全局样式表，
    // 切换时整体重新 setStyleSheet —— 比逐控件 setPalette 更省心，也覆盖未来新增控件。
    auto applyAppPalette = [baseQss]() {
        const bool isDark = (eTheme->getThemeMode() == ElaThemeType::Dark);

        QString textColor, placeholderColor, menuBg, menuBorder, menuHover;
        if (isDark) {
            textColor        = QStringLiteral("#e0e0e0");
            placeholderColor = QStringLiteral("#888888");
            menuBg           = QStringLiteral("#2a2a2a");
            menuBorder       = QStringLiteral("rgba(255,255,255,0.10)");
            menuHover        = QStringLiteral("rgba(255,255,255,0.10)");
        } else {
            textColor        = QStringLiteral("#1a1a1a");
            placeholderColor = QStringLiteral("#999999");
            menuBg           = QStringLiteral("#ffffff");
            menuBorder       = QStringLiteral("rgba(0,0,0,0.10)");
            menuHover        = QStringLiteral("rgba(0,0,0,0.06)");
        }

        QPalette pal = qApp->palette();
        pal.setColor(QPalette::WindowText,      QColor(textColor));
        pal.setColor(QPalette::Text,            QColor(textColor));
        pal.setColor(QPalette::ButtonText,      QColor(textColor));
        pal.setColor(QPalette::ToolTipText,     QColor(textColor));
        pal.setColor(QPalette::PlaceholderText, QColor(placeholderColor));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        qApp->setPalette(pal);

        // 主题相关 QSS：每次主题切换重新拼接到 baseQss 后整体 setStyleSheet。
        // - 输入控件 / ComboBox 显式设 color：ElaWindow 覆盖子控件 palette 后，
        //   不显式声明会拿到旧的 black/white，夜间模式下文字隐没。
        //   ElaComboBox 继承自 QComboBox，QComboBox 选择器也能命中。
        // - QMenu 系列：全局右键菜单（消息气泡 / 会话列表 / 系统托盘）字体跟主题。
        const QString themeQss = QStringLiteral(
            "QPlainTextEdit, QSpinBox, QDoubleSpinBox, QLineEdit, QTextEdit, "
            "QComboBox, QComboBox QAbstractItemView { color: %1; }"
            "QMenu { background-color: %2; color: %1; "
            "        border: 1px solid %3; padding: 4px; }"
            "QMenu::item { padding: 6px 18px; border-radius: 4px; }"
            "QMenu::item:selected { background-color: %4; color: %1; }"
            "QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }"
        ).arg(textColor, menuBg, menuBorder, menuHover);

        qApp->setStyleSheet(baseQss + themeQss);
    };
    applyAppPalette();
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, qApp, applyAppPalette);

    AppSettings settings;

    // 把内置 LLM 工具注册到全局 ToolRegistry。注册完成后日志打印一次当前
    // 已注册的工具名列表，方便启动期确认编译链通且工具集如预期。
    registerFileSystemTools();
    qInfo() << "Tool registry initialized:" << ToolRegistry::instance().toolNames();

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

#ifdef Q_OS_WIN
    // 注册全局热键 Ctrl+Alt+L 唤醒/隐藏主窗口
    GlobalHotkeyFilter hotkeyFilter(&mainWindow);
    app.installNativeEventFilter(&hotkeyFilter);
    RegisterHotKey(nullptr, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'L');
#endif

    mainWindow.show();

    int ret = app.exec();

#ifdef Q_OS_WIN
    UnregisterHotKey(nullptr, HOTKEY_ID);
#endif

    return ret;
}
