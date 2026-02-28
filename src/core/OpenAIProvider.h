#pragma once
#include "LLMProvider.h"

/**
 * @brief OpenAI Chat Completions API 的具体实现类
 *
 * 继承自 LLMProvider，实现了 OpenAI 兼容的流式聊天请求。
 * 同时兼容所有遵循 OpenAI API 格式的第三方服务（如 DeepSeek、本地部署等），
 * 只需修改 baseUrl 即可对接不同后端。
 *
 * OpenAI API 的特点：
 *   - system prompt 作为 messages 数组中 role="system" 的消息传递
 *   - 使用 Bearer Token 方式鉴权（Authorization 头）
 *   - SSE 事件通过 choices[0].delta.content 结构返回增量文本
 */
class OpenAIProvider : public LLMProvider
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param nam     共享的网络访问管理器
     * @param apiKey  OpenAI API 密钥
     * @param baseUrl API 基础地址（如 "https://api.openai.com"）
     * @param model   模型标识符（如 "gpt-4o"）
     * @param parent  父 QObject
     */
    OpenAIProvider(QNetworkAccessManager *nam,
                   const QString &apiKey,
                   const QString &baseUrl,
                   const QString &model,
                   QObject *parent = nullptr);

    /**
     * @brief 发送流式聊天请求到 OpenAI Chat Completions API
     *
     * 构建符合 OpenAI API 规范的 JSON 请求体，
     * system prompt 作为 messages 数组的首条消息插入。
     */
    void sendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) override;

    /** @brief 返回提供者名称 "openai" */
    QString providerName() const override { return "openai"; }

    /** @brief 动态更新 API 密钥 */
    void setApiKey(const QString &key) { m_apiKey = key; }
    /** @brief 动态更新 API 基础地址 */
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    /** @brief 动态更新使用的模型 */
    void setModel(const QString &model) { m_model = model; }

protected:
    /**
     * @brief 解析 OpenAI SSE 事件的 data 字段
     *
     * OpenAI 的流式响应中，增量文本位于 choices[0].delta.content。
     *
     * @param data  SSE 事件的原始 JSON 数据
     * @return 提取到的文本 token，无内容时返回空字符串
     */
    QString parseSSEData(const QByteArray &data) override;

private:
    QString m_apiKey;   // OpenAI API 密钥
    QString m_baseUrl;  // API 基础地址
    QString m_model;    // 当前使用的模型标识符
};
