#include "SettingGeneralPage.h"
#include "ui_SettingGeneralPage.h"
#include "core/AppSettings.h"
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>

SettingGeneralPage::SettingGeneralPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingGeneralPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    loadSettings();
    connectAutoSave();
}

SettingGeneralPage::~SettingGeneralPage()
{
    delete ui;
}

void SettingGeneralPage::loadSettings()
{
    ui->plainTextEdit_systemPrompt->setPlainText(m_settings->systemPrompt());
    ui->doubleSpinBox_temperature->setValue(m_settings->temperature());
    ui->spinBox_maxTokens->setValue(m_settings->maxTokens());
}

void SettingGeneralPage::connectAutoSave()
{
    connect(ui->plainTextEdit_systemPrompt, &QPlainTextEdit::textChanged, this, [this]() {
        m_settings->setSystemPrompt(ui->plainTextEdit_systemPrompt->toPlainText());
        emit settingsChanged();
    });
    connect(ui->doubleSpinBox_temperature, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
        m_settings->setTemperature(val);
        emit settingsChanged();
    });
    connect(ui->spinBox_maxTokens, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int val) {
        m_settings->setMaxTokens(val);
        emit settingsChanged();
    });
}
