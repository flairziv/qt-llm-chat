#include "SettingGeminiPage.h"
#include "ui_SettingGeminiPage.h"
#include "ProviderSettingHelpers.h"
#include "core/AppSettings.h"

SettingGeminiPage::SettingGeminiPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingGeminiPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);

    // 允许手填模型名（Gemini 新模型发布较快）
    ui->comboBox_model->setEditable(true);

    using namespace ProviderSettingHelpers;
    AppSettings *s = m_settings;

    bindLineEdit(ui->lineEdit_apiKey,
                 [s] { return s->geminiApiKey(); },
                 [s](const QString &t) { s->setGeminiApiKey(t); });
    bindLineEdit(ui->lineEdit_baseUrl,
                 [s] { return s->geminiBaseUrl(); },
                 [s](const QString &t) { s->setGeminiBaseUrl(t); });
    bindComboBox(ui->comboBox_model,
                 [s] { return s->geminiModel(); },
                 [s](const QString &t) { s->setGeminiModel(t); });
}

SettingGeminiPage::~SettingGeminiPage()
{
    delete ui;
}
