#pragma once
#include "LLMProvider.h"

#include <QSet>

#include <functional>

/**
 * @brief OpenAI / OpenAI 兼容服务的 LLM Provider 实现
 *
 * 内部按 m_imageApiMode 与模型名分派到三条独立请求路径，每条路径都是一个
 * 私有 sendXxxRequest()（构造请求体）+ extractXxxPayload()（解析响应）：
 *
 *   ┌────────────────────────┬────────────────────────────────────────┐
 *   │ m_imageApiMode         │ 走的端点                                 │
 *   ├────────────────────────┼────────────────────────────────────────┤
 *   │ "responses"            │ /v1/responses + image_generation tool   │
 *   │ "images"               │ /v1/images/generations                  │
 *   │ "edits"                │ /v1/images/edits                        │
 *   │ "chat" / 其他          │ /v1/chat/completions                    │
 *   │   └ 图片模型分支       │   非流式 JSON，复杂宽匹配解析             │
 *   │   └ 普通对话分支       │   SSE 流式，走基类 connectReplySignals     │
 *   └────────────────────────┴────────────────────────────────────────┘
 *
 * 三条非流式 image 路径共享一个 postImageRequest() 处理 reply 生命周期：
 * abort race / HTTP 错误 / 成功 emit；调用方只需提供 parsePayload 回调。
 *
 * 图片输出去重：m_emittedImageHashes 记录已发出的 SHA-1 摘要（路径 / URL 两类），
 * 避免同一图被多源字段（url / b64_json / base64 / image_base64 / image_url）
 * 重复发出。tryEmitImagePath / tryEmitImageUrl 是去重写入辅助。
 */
class OpenAIProvider : public LLMProvider
{
    Q_OBJECT
public:
    OpenAIProvider(QNetworkAccessManager *nam,
                   const QString &apiKey,
                   const QString &baseUrl,
                   const QString &model,
                   const QString &reasoningEffort = QString(),
                   const QString &imageApiMode = QStringLiteral("images"),
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
    void setImageApiMode(const QString &mode) { m_imageApiMode = mode; }

protected:
    LLMProviderParseResult parseSSEData(const QByteArray &data) override;

private:
    // 图片落盘工具
    QString saveBase64ImageToFile(const QString &base64Data);
    QString saveImageBytesToFile(const QByteArray &bytes);

    // 鉴权 / 端点构造
    bool ensureApiKeyConfigured();                                            // baseUrl 指向 openai.com 但 apiKey 为空时报错
    QNetworkRequest buildAuthorizedRequest(const QUrl &url,
                                           bool applyImageTimeout = false);   // 在 makeJsonRequest 之上追加 Bearer 头与可选超时

    // 图片输出去重（多源同图、URL 与 base64 互相覆盖时只发一次）
    bool tryEmitImagePath(const QString &savedPath, QString &output);
    bool tryEmitImageUrl(const QString &url, QString &output);

    // 三条独立请求路径
    void sendResponsesImageRequest(const QList<ChatMessage> &messages);       // /v1/responses + image_generation tool
    void sendImagesGenerationRequest(const QList<ChatMessage> &messages);     // /v1/images/generations
    void sendImagesEditRequest(const QList<ChatMessage> &messages);           // /v1/images/edits
    void sendChatRequest(const QList<ChatMessage> &messages,
                         const QString &systemPrompt,
                         int maxTokens,
                         double temperature);                                  // /v1/chat/completions（流式或图片模型）

    // 三条非流式 image 路径共享的 reply 生命周期：
    //   - abort race（reply != m_currentReply 时只 deleteLater 不发信号）
    //   - HTTP/网络错误（提取 API 错误消息后 emit errorOccurred）
    //   - 成功时调用 parsePayload 取展示文本，发 tokenReceived/responseFinished 并清理
    // 取代之前每条路径各写一份的 finished lambda。
    void postImageRequest(const QNetworkRequest &request,
                          const QByteArray &body,
                          std::function<QString(const QByteArray&)> parsePayload,
                          int placeholderCount = 1);

    // 各路径的 payload 解析（仅由对应 sendXxx 调用，独立成员是为了让 sendXxx 体保持紧凑）
    QString extractResponsesPayload(const QByteArray &payload);
    QString extractImagesPayload(const QByteArray &payload);
    QString extractChatImagePayload(const QByteArray &payload);

    QString m_apiKey;
    QString m_baseUrl;
    QString m_model;
    QString m_reasoningEffort;
    QString m_imageApiMode;
    QSet<QByteArray> m_emittedImageHashes;
};
