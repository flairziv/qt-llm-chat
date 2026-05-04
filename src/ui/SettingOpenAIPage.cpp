#include "SettingOpenAIPage.h"
#include "ui_SettingOpenAIPage.h"
#include "ProviderSettingHelpers.h"
#include "core/AppSettings.h"

SettingOpenAIPage::SettingOpenAIPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingOpenAIPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);

    // 允许手填模型名（支持 Ollama 等本地模型如 "llama3.2"）
    ui->comboBox_model->setEditable(true);

    using namespace ProviderSettingHelpers;
    AppSettings *s = m_settings;

    bindLineEdit(ui->lineEdit_apiKey,
                 [s] { return s->openaiApiKey(); },
                 [s](const QString &t) { s->setOpenaiApiKey(t); });
    bindLineEdit(ui->lineEdit_baseUrl,
                 [s] { return s->openaiBaseUrl(); },
                 [s](const QString &t) { s->setOpenaiBaseUrl(t); });
    bindComboBox(ui->comboBox_model,
                 [s] { return s->openaiModel(); },
                 [s](const QString &t) { s->setOpenaiModel(t); });
    bindReasoningComboBox(ui->comboBox_reasoning,
                          [s] { return s->openaiReasoningEffort(); },
                          [s](const QString &t) { s->setOpenaiReasoningEffort(t); });
}

SettingOpenAIPage::~SettingOpenAIPage()
{
    delete ui;
}
