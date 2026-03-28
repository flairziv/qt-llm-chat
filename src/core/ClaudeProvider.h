#pragma once
#include "LLMProvider.h"

class ClaudeProvider : public LLMProvider
{
    Q_OBJECT
public:
    ClaudeProvider(QNetworkAccessManager *nam,
                   const QString &apiKey,
                   const QString &baseUrl,
                   const QString &model,
                   QObject *parent = nullptr);

    void sendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) override;

    QString providerName() const override { return "claude"; }
    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    void setModel(const QString &model) { m_model = model; }

protected:
    QString parseSSEData(const QByteArray &data) override;

private:
    QString m_apiKey;
    QString m_baseUrl;
    QString m_model;
};
