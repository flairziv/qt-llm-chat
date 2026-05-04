#pragma once
#include "LLMProvider.h"

class OpenAIProvider : public LLMProvider
{
    Q_OBJECT
public:
    OpenAIProvider(QNetworkAccessManager *nam,
                   const QString &apiKey,
                   const QString &baseUrl,
                   const QString &model,
                   const QString &reasoningEffort = QString(),
                   QObject *parent = nullptr);

    void doSendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) override;

    QString providerName() const override { return "openai"; }

    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    void setModel(const QString &model) { m_model = model; }
    void setReasoningEffort(const QString &effort) { m_reasoningEffort = effort; }

protected:
    LLMProviderParseResult parseSSEData(const QByteArray &data) override;

private:
    QString m_apiKey;
    QString m_baseUrl;
    QString m_model;
    QString m_reasoningEffort;
};