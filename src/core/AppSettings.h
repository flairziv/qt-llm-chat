#pragma once
#include <QObject>
#include <QSettings>
#include <QString>

class AppSettings : public QObject
{
    Q_OBJECT
public:
    explicit AppSettings(QObject *parent = nullptr);

    QString claudeApiKey() const;
    void setClaudeApiKey(const QString &key);
    QString claudeBaseUrl() const;
    void setClaudeBaseUrl(const QString &url);
    QString claudeModel() const;
    void setClaudeModel(const QString &model);

    QString openaiApiKey() const;
    void setOpenaiApiKey(const QString &key);
    QString openaiBaseUrl() const;
    void setOpenaiBaseUrl(const QString &url);
    QString openaiModel() const;
    void setOpenaiModel(const QString &model);

    QString activeProvider() const;
    void setActiveProvider(const QString &provider);
    QString systemPrompt() const;
    void setSystemPrompt(const QString &prompt);
    int maxTokens() const;
    void setMaxTokens(int tokens);
    double temperature() const;
    void setTemperature(double temp);

    QString dataDir() const;

signals:
    void settingsChanged();

private:
    QSettings *m_settings;
    QString m_dataDir;
};
