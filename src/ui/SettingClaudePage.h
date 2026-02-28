#pragma once
#include <QWidget>

class AppSettings;

namespace Ui { class SettingClaudePage; }

/**
 * @brief Claude API 设置页面
 *
 * 使用 Qt Designer 的 .ui 文件定义界面布局（SettingClaudePage.ui），
 * 通过 CMAKE_AUTOUIC 自动生成 ui_SettingClaudePage.h 头文件。
 *
 * .ui 文件与 C++ 的关系：
 *   SettingClaudePage.ui  ──(uic 编译)──→ ui_SettingClaudePage.h
 *        (XML 布局)                         (生成的 C++ 类 Ui::SettingClaudePage)
 *                                                  ↓
 *   SettingClaudePage.cpp 中通过 ui->setupUi(this) 实例化界面
 *   通过 ui->lineEdit_apiKey / ui->comboBox_model 等访问控件
 *
 * 功能：
 * - 加载并显示当前 Claude API 配置（API Key、Base URL、Model）
 * - 用户修改任一字段后自动保存到 AppSettings 并发射 settingsChanged 信号
 */
class SettingClaudePage : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param settings  全局配置对象，用于读写 Claude API 设置
     * @param parent    父 widget
     */
    explicit SettingClaudePage(AppSettings *settings, QWidget *parent = nullptr);
    ~SettingClaudePage();

    /** @brief 从 AppSettings 加载配置并更新 UI 控件 */
    void loadSettings();

signals:
    /** @brief 任一设置项被修改时发射，通知 MainWindow 重建 Provider */
    void settingsChanged();

private:
    /** @brief 将每个输入控件的 textChanged 信号连接到自动保存逻辑 */
    void connectAutoSave();

    Ui::SettingClaudePage *ui;  // 由 .ui 文件生成的界面对象指针
    AppSettings *m_settings;    // 全局配置（不持有所有权）
};
