#include "AppSettings.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(m_dataDir);
    QDir().mkpath(m_dataDir + "/sessions");
    m_settings = new QSettings(m_dataDir + "/settings.ini", QSettings::IniFormat, this);
    m_settings->setValue("General/active_provider",
                         m_settings->value("General/active_provider", "openai").toString());
    m_settings->sync();
}

QString AppSettings::claudeApiKey() const { return m_settings->value("Claude/api_key").toString(); }
void AppSettings::setClaudeApiKey(const QString &key) { m_settings->setValue("Claude/api_key", key); m_settings->sync(); emit settingsChanged(); }
QString AppSettings::claudeBaseUrl() const { return m_settings->value("Claude/base_url", "https://api.anthropic.com").toString(); }
void AppSettings::setClaudeBaseUrl(const QString &url) { m_settings->setValue("Claude/base_url", url); m_settings->sync(); emit settingsChanged(); }
QString AppSettings::claudeModel() const { return m_settings->value("Claude/model", "claude-sonnet-4-20250514").toString(); }
void AppSettings::setClaudeModel(const QString &model) { m_settings->setValue("Claude/model", model); m_settings->sync(); emit settingsChanged(); }

QString AppSettings::openaiApiKey() const { return m_settings->value("OpenAI/api_key").toString(); }
void AppSettings::setOpenaiApiKey(const QString &key) { m_settings->setValue("OpenAI/api_key", key); m_settings->sync(); emit settingsChanged(); }
QString AppSettings::openaiBaseUrl() const { return m_settings->value("OpenAI/base_url", "https://api.openai.com").toString(); }
void AppSettings::setOpenaiBaseUrl(const QString &url) { m_settings->setValue("OpenAI/base_url", url); m_settings->sync(); emit settingsChanged(); }
QString AppSettings::openaiModel() const { return m_settings->value("OpenAI/model", "gpt-4o").toString(); }
void AppSettings::setOpenaiModel(const QString &model) { m_settings->setValue("OpenAI/model", model); m_settings->sync(); emit settingsChanged(); }

QString AppSettings::activeProvider() const { return m_settings->value("General/active_provider", "openai").toString(); }
void AppSettings::setActiveProvider(const QString &provider) { m_settings->setValue("General/active_provider", provider); m_settings->sync(); emit settingsChanged(); }
QString AppSettings::systemPrompt() const { return m_settings->value("General/system_prompt", "You are a helpful assistant.").toString(); }
void AppSettings::setSystemPrompt(const QString &prompt) { m_settings->setValue("General/system_prompt", prompt); m_settings->sync(); emit settingsChanged(); }
int AppSettings::maxTokens() const { return m_settings->value("General/max_tokens", 4096).toInt(); }
void AppSettings::setMaxTokens(int tokens) { m_settings->setValue("General/max_tokens", tokens); m_settings->sync(); emit settingsChanged(); }
double AppSettings::temperature() const { return m_settings->value("General/temperature", 0.7).toDouble(); }
void AppSettings::setTemperature(double temp) { m_settings->setValue("General/temperature", temp); m_settings->sync(); emit settingsChanged(); }

QString AppSettings::dataDir() const { return m_dataDir; }
