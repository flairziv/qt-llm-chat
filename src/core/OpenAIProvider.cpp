#include "OpenAIProvider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ============================================================================
// 构造函数
// ============================================================================

OpenAIProvider::OpenAIProvider(QNetworkAccessManager *nam,
                               const QString &apiKey,
                               const QString &baseUrl,
                               const QString &model,
                               QObject *parent)
    : LLMProvider(nam, parent)
    , m_apiKey(apiKey)
    , m_baseUrl(baseUrl)
    , m_model(model)
{
}

// ============================================================================
// 流式请求
// ============================================================================

/**
 * @brief 构建并发送 OpenAI Chat Completions API 的流式请求
 *
 * 请求流程：
 * 1. 清空上一次的累积响应和 SSE 解析器状态
 * 2. 校验 API 密钥是否已配置
 * 3. 构建 HTTP 请求头：
 *    - Content-Type: application/json
 *    - Authorization: Bearer <apiKey>（OpenAI 标准鉴权方式）
 * 4. 构建请求体 JSON：
 *    - system prompt 作为 messages 数组的第一条消息（role="system"）
 *    - 随后依次追加用户传入的聊天消息
 *    - stream: true 启用流式输出
 * 5. 发送 POST 请求并连接回复信号
 */
void OpenAIProvider::sendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    // 重置状态，为新一轮请求做准备
    m_accumulatedResponse.clear();
    m_sseParser.reset();

    // API 密钥校验
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("OpenAI API key is not configured. Please set it in Settings.");
        return;
    }

    // 构建 HTTP 请求
    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());  // Bearer Token 鉴权

    // 构建请求体
    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["stream"] = true;

    // OpenAI API 规范：system prompt 作为 messages 数组的首条消息
    QJsonArray msgArray;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt;
        msgArray.append(sysMsg);
    }

    // 追加聊天历史消息
    for (const auto &msg : messages) {
        QJsonObject m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgArray.append(m);
    }
    body["messages"] = msgArray;

    // 发送请求并绑定信号
    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connectReplySignals(reply);
}

// ============================================================================
// SSE 数据解析
// ============================================================================

/**
 * @brief 从 OpenAI SSE 事件的 JSON 数据中提取增量文本
 *
 * OpenAI 的流式响应格式：
 * {
 *   "choices": [{
 *     "delta": {
 *       "content": "增量文本"    // 仅在有新内容时存在此字段
 *     },
 *     "finish_reason": null      // 结束时为 "stop"
 *   }]
 * }
 *
 * 解析逻辑：
 * 1. 解析 JSON，获取 choices 数组
 * 2. 取第一个 choice 的 delta 对象
 * 3. 若 delta 中包含 "content" 字段，返回其文本值
 */
QString OpenAIProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) return {};

    // 取第一个 choice 的增量内容
    QJsonObject delta = choices[0].toObject()["delta"].toObject();
    if (delta.contains("content")) {
        return delta["content"].toString();
    }

    return {};
}
