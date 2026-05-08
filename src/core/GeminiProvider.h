#pragma once
#include "LLMProvider.h"

/**
 * @brief Google Gemini API 提供者
 *
 * 通过 Google AI Studio 的 REST API 发起流式请求：
 *   POST https://generativelanguage.googleapis.com/v1beta/models/{model}:streamGenerateContent?alt=sse&key={API_KEY}
 *
 * 请求/响应格式与 Claude/OpenAI 完全不同：
 * - 使用 contents 数组（role="user"/"model"，parts=[{text/inlineData}]）
 * - 支持多模态（inlineData 嵌入 base64 图片）
 * - systemInstruction 作为独立字段
 * - 返回 candidates[0].content.parts[0].text
 */
class GeminiProvider : public LLMProvider
{
    Q_OBJECT
public:
    GeminiProvider(QNetworkAccessManager *nam,
                   const QString &apiKey,
                   const QString &baseUrl,
                   const QString &model,
                   QObject *parent = nullptr);

    void doSendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) override;

    QString providerName() const override { return "gemini"; }

    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    void setModel(const QString &model) { m_model = model; }

protected:
    LLMProviderParseResult parseSSEData(const QByteArray &data) override;

private:
    QString m_apiKey;
    QString m_baseUrl;
    QString m_model;
};
