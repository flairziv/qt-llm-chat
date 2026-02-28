#include "SettingClaudePage.h"
#include "ui_SettingClaudePage.h"   // 由 uic 从 SettingClaudePage.ui 自动生成
#include "core/AppSettings.h"
#include <QLineEdit>
#include <QComboBox>

// ============================================================================
// 构造 / 析构
// ============================================================================

SettingClaudePage::SettingClaudePage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingClaudePage)
    , m_settings(settings)
{
    ui->setupUi(this);    // 实例化 .ui 文件定义的界面布局
    loadSettings();        // 从配置加载当前值到 UI
    connectAutoSave();     // 建立自动保存连接
}

SettingClaudePage::~SettingClaudePage()
{
    delete ui;  // 释放 .ui 生成的界面对象
}

// ============================================================================
// 配置加载
// ============================================================================

/**
 * @brief 从 AppSettings 读取 Claude 配置并更新到 UI 控件
 *
 * 对于 Model 下拉框：
 * - 优先在预设列表中查找匹配项
 * - 若设置中的模型不在预设列表中（如用户手动填过自定义模型），
 *   则动态添加到下拉框并选中
 */
void SettingClaudePage::loadSettings()
{
    ui->lineEdit_apiKey->setText(m_settings->claudeApiKey());
    ui->lineEdit_baseUrl->setText(m_settings->claudeBaseUrl());

    // 在预设列表中查找当前模型
    QString model = m_settings->claudeModel();
    int idx = ui->comboBox_model->findText(model);
    if (idx >= 0) {
        ui->comboBox_model->setCurrentIndex(idx);
    } else if (!model.isEmpty()) {
        // 自定义模型名：动态追加到下拉框
        ui->comboBox_model->addItem(model);
        ui->comboBox_model->setCurrentText(model);
    }
}

// ============================================================================
// 自动保存
// ============================================================================

/**
 * @brief 将每个输入控件的变更信号连接到自动保存 lambda
 *
 * 用户修改任一字段后：
 * 1. 立即写入 AppSettings（持久化）
 * 2. 发射 settingsChanged() 信号 → MainWindow 收到后重建 LLMProvider
 */
void SettingClaudePage::connectAutoSave()
{
    // API Key 变更 → 保存并通知
    connect(ui->lineEdit_apiKey, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setClaudeApiKey(text);
        emit settingsChanged();
    });
    // Base URL 变更 → 保存并通知
    connect(ui->lineEdit_baseUrl, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setClaudeBaseUrl(text);
        emit settingsChanged();
    });
    // Model 选择变更 → 保存并通知
    connect(ui->comboBox_model, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_settings->setClaudeModel(text);
        emit settingsChanged();
    });
}
