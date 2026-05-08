#include "GeminiProvider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

GeminiProvider::GeminiProvider(QNetworkAccessManager *nam,
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

/**
 * @brief 构建 Gemini 请求并发起流式请求
 *
 * Gemini API 与 OpenAI/Claude 的关键差异：
 * - role 只有 "user" 和 "model"（非 "assistant"）
 * - 内容嵌套在 parts 数组中：{text:"..."} 或 {inlineData:{mimeType,data}}
 * - systemInstruction 是顶层字段
 * - generationConfig 包含 maxOutputTokens 和 temperature
 */
void GeminiProvider::doSendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    m_accumulatedResponse.clear();
    m_sseParser.reset();

    if (m_apiKey.isEmpty()) {
        emit errorOccurred("Gemini API key is not configured. Please set it in Settings.");
        return;
    }

    // URL: {baseUrl}/v1beta/models/{model}:streamGenerateContent?alt=sse&key={key}
    QString url = QStringLiteral("%1/v1beta/models/%2:streamGenerateContent?alt=sse&key=%3")
                      .arg(m_baseUrl, m_model, m_apiKey);

    QNetworkRequest request = makeJsonRequest(QUrl(url));

    QJsonObject body;

    // 生成配置
    QJsonObject genConfig;
    genConfig["maxOutputTokens"] = maxTokens;
    genConfig["temperature"] = temperature;
    body["generationConfig"] = genConfig;

    // System instruction（可选）
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysInst;
        QJsonArray sysParts;
        QJsonObject sysPart;
        sysPart["text"] = systemPrompt;
        sysParts.append(sysPart);
        sysInst["parts"] = sysParts;
        body["systemInstruction"] = sysInst;
    }

    // Contents：消息列表
    QJsonArray contents;
    for (const auto &msg : messages) {
        if (msg.role == "system") continue;

        QJsonObject content;
        // Gemini 用 "user" 和 "model"，需要从 "assistant" 转换
        content["role"] = (msg.role == "assistant") ? QStringLiteral("model") : QStringLiteral("user");

        QJsonArray parts;

        // 附件（图片）作为 inlineData
        for (const auto &att : msg.attachments) {
            if (att.type == Attachment::Image) {
                QJsonObject inlinePart;
                QJsonObject inlineData;
                inlineData["mimeType"] = att.mimeType;
                inlineData["data"] = QString::fromLatin1(att.fileData.toBase64());
                inlinePart["inlineData"] = inlineData;
                parts.append(inlinePart);
            }
            // 文本文件内容合并到文本部分（下面处理）
        }

        // 组合文本内容（文本文件 + Document 占位提示 + 用户消息）
        QString textPart = flattenTextAttachments(msg.attachments);
        for (const auto &att : msg.attachments) {
            if (att.type == Attachment::Document) {
                textPart += QStringLiteral("[Document: %1 (binary, not supported)]\n\n").arg(att.fileName);
            }
        }
        textPart += msg.content;

        if (!textPart.trimmed().isEmpty()) {
            QJsonObject tp;
            tp["text"] = textPart;
            parts.append(tp);
        }

        if (!parts.isEmpty()) {
            content["parts"] = parts;
            contents.append(content);
        }
    }
    body["contents"] = contents;

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connectReplySignals(reply);
}

/**
 * @brief 解析 Gemini SSE 事件
 *
 * 每个 SSE event 的 data 是 JSON：
 *   {"candidates":[{"content":{"parts":[{"text":"xxx"}],"role":"model"},...}]}
 * 提取 parts[0].text 作为 token 返回。
 */
LLMProviderParseResult GeminiProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QJsonArray candidates = obj["candidates"].toArray();
    if (candidates.isEmpty()) return {};

    QJsonObject content = candidates[0].toObject()["content"].toObject();
    QJsonArray parts = content["parts"].toArray();
    if (parts.isEmpty()) return {};

    LLMProviderParseResult result;
    result.contentToken = parts[0].toObject()["text"].toString();
    return result;
}
