#pragma once
#include "LLMProvider.h"
#include <QHash>

class AppSettings;

class ClaudeProvider : public LLMProvider
{
    Q_OBJECT
public:
    ClaudeProvider(QNetworkAccessManager *nam,
                   AppSettings *settings,
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
    // tools 数组过滤用：总开关 toolsEnabled() + 按工具 toolEnabled(name)。请求时实时读，
    // 改设置无需重建本对象（createProvider 仍会在 provider 配置变更时重建，无妨）。
    // 声明在最前，配合初始化列表最先初始化，避免 /W4 的 C5038 成员初始化顺序告警。
    AppSettings *m_settings;
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