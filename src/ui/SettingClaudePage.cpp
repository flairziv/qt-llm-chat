#include "SettingClaudePage.h"
#include "ui_SettingClaudePage.h"
#include "ProviderSettingHelpers.h"
#include "core/AppSettings.h"

SettingClaudePage::SettingClaudePage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingClaudePage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);

    using namespace ProviderSettingHelpers;
    AppSettings *s = m_settings;

    // 让 Claude 模型下拉框允许手动输入（与 OpenAI / Gemini 行为一致），
    // 方便填新发布但 .ui 里还没列的型号或第三方代理转发的别名。
    ui->comboBox_model->setEditable(true);

    bindLineEdit(ui->lineEdit_apiKey,
                 [s] { return s->claudeApiKey(); },
                 [s](const QString &t) { s->setClaudeApiKey(t); });
    bindLineEdit(ui->lineEdit_baseUrl,
                 [s] { return s->claudeBaseUrl(); },
                 [s](const QString &t) { s->setClaudeBaseUrl(t); });
    bindComboBox(ui->comboBox_model,
                 [s] { return s->claudeModel(); },
                 [s](const QString &t) { s->setClaudeModel(t); });
    bindReasoningComboBox(ui->comboBox_reasoning,
                          [s] { return s->claudeReasoningEffort(); },
                          [s](const QString &t) { s->setClaudeReasoningEffort(t); });
}

SettingClaudePage::~SettingClaudePage()
{
    delete ui;
}
