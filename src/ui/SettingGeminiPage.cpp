#include "SettingGeminiPage.h"
#include "ui_SettingGeminiPage.h"

#include "core/AppSettings.h"

#include <QComboBox>
#include <QLineEdit>

SettingGeminiPage::SettingGeminiPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingGeminiPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);
    ui->comboBox_model->setEditable(true);

    loadSettings();
    connectAutoSave();
}

SettingGeminiPage::~SettingGeminiPage()
{
    delete ui;
}

void SettingGeminiPage::loadSettings()
{
    ui->lineEdit_apiKey->setText(m_settings->geminiApiKey());
    ui->lineEdit_baseUrl->setText(m_settings->geminiBaseUrl());

    const QString model = m_settings->geminiModel();
    const int idx = ui->comboBox_model->findText(model);
    if (idx >= 0) {
        ui->comboBox_model->setCurrentIndex(idx);
    } else if (!model.isEmpty()) {
        ui->comboBox_model->addItem(model);
        ui->comboBox_model->setCurrentText(model);
    }
}

void SettingGeminiPage::connectAutoSave()
{
    connect(ui->lineEdit_apiKey, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setGeminiApiKey(text);
    });
    connect(ui->lineEdit_baseUrl, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setGeminiBaseUrl(text);
    });
    connect(ui->comboBox_model, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_settings->setGeminiModel(text);
    });
}
