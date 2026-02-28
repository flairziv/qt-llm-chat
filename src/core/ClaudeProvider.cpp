#include "ClaudeProvider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ============================================================================
// 构造函数
// ============================================================================

ClaudeProvider::ClaudeProvider(QNetworkAccessManager *nam,
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
 * @brief 构建并发送 Claude Messages API 的流式请求
 *
 * 请求流程：
 * 1. 清空上一次的累积响应和 SSE 解析器状态
 * 2. 校验 API 密钥是否已配置
 * 3. 构建 HTTP 请求头：
 *    - Content-Type: application/json
 *    - x-api-key: Claude 专用的鉴权头
 *    - anthropic-version: API 版本标识
 * 4. 构建请求体 JSON：
 *    - system prompt 作为顶层 "system" 字段（Claude API 规范）
 *    - messages 数组只包含 user 和 assistant 角色，过滤掉 system 角色
 *    - stream: true 启用流式输出
 * 5. 发送 POST 请求并连接回复信号
 */
void ClaudeProvider::sendStreamingRequest(
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
        emit errorOccurred("Claude API key is not configured. Please set it in Settings.");
        return;
    }

    // 构建 HTTP 请求
    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/messages"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());          // Claude 使用自定义头鉴权
    request.setRawHeader("anthropic-version", "2023-06-01");       // API 版本

    // 构建请求体
    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["stream"] = true;

    // Claude API 特有：system prompt 是顶层字段，不在 messages 数组中
    if (!systemPrompt.isEmpty()) {
        body["system"] = systemPrompt;
    }

    // 构建消息数组，过滤掉 system 角色（已通过顶层字段传递）
    QJsonArray msgArray;
    for (const auto &msg : messages) {
        if (msg.role == "system") continue;
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
 * @brief 从 Claude SSE 事件的 JSON 数据中提取增量文本
 *
 * Claude 的 SSE 流使用 "type" 字段标识事件类型：
 *   - "message_start"       : 消息开始
 *   - "content_block_start" : 内容块开始
 *   - "content_block_delta" : 增量内容（包含实际文本）
 *   - "content_block_stop"  : 内容块结束
 *   - "message_delta"       : 消息级别的增量更新（如 stop_reason）
 *   - "message_stop"        : 消息结束
 *
 * 只有 "content_block_delta" 类型且 delta.type == "text_delta" 时，
 * 才从 delta.text 中提取文本 token。
 */
QString ClaudeProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    // 只处理文本增量事件
    if (type == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        if (delta["type"].toString() == "text_delta") {
            return delta["text"].toString();
        }
    }

    return {};
}
