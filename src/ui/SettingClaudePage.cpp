#include "SettingClaudePage.h"
#include "ui_SettingClaudePage.h"
#include "core/AppSettings.h"
#include <QComboBox>
#include <QLineEdit>

SettingClaudePage::SettingClaudePage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingClaudePage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);
    loadSettings();
    connectAutoSave();
}

SettingClaudePage::~SettingClaudePage()
{
    delete ui;
}

void SettingClaudePage::loadSettings()
{
    ui->lineEdit_apiKey->setText(m_settings->claudeApiKey());
    ui->lineEdit_baseUrl->setText(m_settings->claudeBaseUrl());

    QString model = m_settings->claudeModel();
    int idx = ui->comboBox_model->findText(model);
    if (idx >= 0) {
        ui->comboBox_model->setCurrentIndex(idx);
    } else if (!model.isEmpty()) {
        ui->comboBox_model->addItem(model);
        ui->comboBox_model->setCurrentText(model);
    }

    QString reasoningEffort = m_settings->claudeReasoningEffort();
    if (reasoningEffort.isEmpty()) {
        reasoningEffort = "default";
    }
    idx = ui->comboBox_reasoning->findText(reasoningEffort);
    if (idx >= 0) {
        ui->comboBox_reasoning->setCurrentIndex(idx);
    } else {
        ui->comboBox_reasoning->addItem(reasoningEffort);
        ui->comboBox_reasoning->setCurrentText(reasoningEffort);
    }
}

void SettingClaudePage::connectAutoSave()
{
    connect(ui->lineEdit_apiKey, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setClaudeApiKey(text);
        emit settingsChanged();
    });
    connect(ui->lineEdit_baseUrl, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setClaudeBaseUrl(text);
        emit settingsChanged();
    });
    connect(ui->comboBox_model, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_settings->setClaudeModel(text);
        emit settingsChanged();
    });
    connect(ui->comboBox_reasoning, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_settings->setClaudeReasoningEffort(text == "default" ? QString() : text);
        emit settingsChanged();
    });
}