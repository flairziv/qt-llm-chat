#include "SettingOpenAIPage.h"
#include "ui_SettingOpenAIPage.h"
#include "core/AppSettings.h"
#include <QLineEdit>
#include <QComboBox>

SettingOpenAIPage::SettingOpenAIPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingOpenAIPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);
    loadSettings();
    connectAutoSave();
}

SettingOpenAIPage::~SettingOpenAIPage()
{
    delete ui;
}

void SettingOpenAIPage::loadSettings()
{
    ui->lineEdit_apiKey->setText(m_settings->openaiApiKey());
    ui->lineEdit_baseUrl->setText(m_settings->openaiBaseUrl());

    QString model = m_settings->openaiModel();
    int idx = ui->comboBox_model->findText(model);
    if (idx >= 0) {
        ui->comboBox_model->setCurrentIndex(idx);
    } else if (!model.isEmpty()) {
        ui->comboBox_model->addItem(model);
        ui->comboBox_model->setCurrentText(model);
    }
}

void SettingOpenAIPage::connectAutoSave()
{
    connect(ui->lineEdit_apiKey, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setOpenaiApiKey(text);
        emit settingsChanged();
    });
    connect(ui->lineEdit_baseUrl, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setOpenaiBaseUrl(text);
        emit settingsChanged();
    });
    connect(ui->comboBox_model, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_settings->setOpenaiModel(text);
        emit settingsChanged();
    });
}
