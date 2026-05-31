#pragma once
#include "LLMProvider.h"
#include <QHash>

class ClaudeProvider : public LLMProvider
{
    Q_OBJECT
public:
    ClaudeProvider(QNetworkAccessManager *nam,
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

    QString providerName() const override { return "claude"; }

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

    /**
     * @brief 流式解析中尚未完成的 tool_use 块，按 content block index 索引
     *
     * Claude 把每个 tool_use 拆成 content_block_start（带 id / name）→ 若干
     * input_json_delta（拼 input JSON）→ content_block_stop，解析跨多条 SSE
     * event，故需在此累积；content_block_stop 时把定稿的 ToolCall 移入基类
     * m_pendingToolCalls。
     */
    QHash<int, ToolCall> m_toolUseBlocks;
};