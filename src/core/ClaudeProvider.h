#pragma once
#include "LLMProvider.h"

/**
 * @brief Claude (Anthropic) API 的具体实现类
 *
 * 继承自 LLMProvider，实现了 Anthropic Messages API 的流式请求。
 * Claude API 的特点：
 *   - system prompt 作为顶层字段传递，而非放在 messages 数组中
 *   - 使用 x-api-key 头进行鉴权
 *   - SSE 事件通过 content_block_delta / text_delta 结构返回增量文本
 */
class ClaudeProvider : public LLMProvider
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param nam     共享的网络访问管理器
     * @param apiKey  Anthropic API 密钥
     * @param baseUrl API 基础地址（如 "https://api.anthropic.com"）
     * @param model   模型标识符（如 "claude-sonnet-4-20250514"）
     * @param parent  父 QObject
     */
    ClaudeProvider(QNetworkAccessManager *nam,
                   const QString &apiKey,
                   const QString &baseUrl,
                   const QString &model,
                   QObject *parent = nullptr);

    /**
     * @brief 发送流式聊天请求到 Claude Messages API
     *
     * 构建符合 Anthropic API 规范的 JSON 请求体，
     * system prompt 放在顶层 "system" 字段，messages 数组只包含 user/assistant 角色。
     */
    void sendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) override;

    /** @brief 返回提供者名称 "claude" */
    QString providerName() const override { return "claude"; }

    /** @brief 动态更新 API 密钥 */
    void setApiKey(const QString &key) { m_apiKey = key; }
    /** @brief 动态更新 API 基础地址 */
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    /** @brief 动态更新使用的模型 */
    void setModel(const QString &model) { m_model = model; }

protected:
    /**
     * @brief 解析 Claude SSE 事件的 data 字段
     *
     * Claude 的流式响应使用 type 字段区分事件类型，
     * 增量文本在 content_block_delta 事件的 delta.text 中。
     *
     * @param data  SSE 事件的原始 JSON 数据
     * @return 提取到的文本 token，无内容时返回空字符串
     */
    QString parseSSEData(const QByteArray &data) override;

private:
    QString m_apiKey;   // Anthropic API 密钥
    QString m_baseUrl;  // API 基础地址
    QString m_model;    // 当前使用的模型标识符
};
