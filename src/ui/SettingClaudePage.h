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
 * 控件↔AppSettings 的双向绑定由 ProviderSettingHelpers 的 inline helper
 * 完成（构造函数里 bindLineEdit / bindComboBox / bindReasoningComboBox）。
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

private:
    Ui::SettingClaudePage *ui;  // 由 .ui 文件生成的界面对象指针
    AppSettings *m_settings;    // 全局配置（不持有所有权）
};
