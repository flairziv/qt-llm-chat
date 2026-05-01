#include "ClaudeProvider.h"
#include "NetworkRequestUtils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace {

QString normalizedReasoningEffort(const QString &effort)
{
    return effort.trimmed().toLower();
}

bool supportsClaudeEffort(const QString &model)
{
    return model.startsWith("claude-opus-4-6")
        || model.startsWith("claude-sonnet-4-6")
        || model.startsWith("claude-opus-4-5");
}

bool usesAdaptiveThinking(const QString &model)
{
    return model.startsWith("claude-opus-4-6")
        || model.startsWith("claude-sonnet-4-6");
}

} // namespace

ClaudeProvider::ClaudeProvider(QNetworkAccessManager *nam,
                               const QString &apiKey,
                               const QString &baseUrl,
                               const QString &model,
                               const QString &reasoningEffort,
                               QObject *parent)
    : LLMProvider(nam, parent)
    , m_apiKey(apiKey)
    , m_baseUrl(baseUrl)
    , m_model(model)
    , m_reasoningEffort(reasoningEffort)
{
}

void ClaudeProvider::sendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    m_accumulatedResponse.clear();
    m_sseParser.reset();

    if (m_apiKey.isEmpty()) {
        emit errorOccurred("Claude API key is not configured. Please set it in Settings.");
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/messages"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    applyGoogleChromeUserAgent(request);
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");

    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["stream"] = true;

    const QString reasoningEffort = normalizedReasoningEffort(m_reasoningEffort);
    if (!reasoningEffort.isEmpty()
        && reasoningEffort != "default"
        && supportsClaudeEffort(m_model)) {
        QJsonObject outputConfig;
        outputConfig["effort"] = reasoningEffort;
        body["output_config"] = outputConfig;

        if (usesAdaptiveThinking(m_model)) {
            QJsonObject thinking;
            thinking["type"] = "adaptive";
            body["thinking"] = thinking;
        }
    }

    if (!systemPrompt.isEmpty()) {
        body["system"] = systemPrompt;
    }

    QJsonArray msgArray;
    for (const auto &msg : messages) {
        if (msg.role == "system") continue;
        QJsonObject m;
        m["role"] = msg.role;

        if (msg.attachments.isEmpty()) {
            m["content"] = msg.content;
        } else {
            QJsonArray contentArray;

            for (const auto &att : msg.attachments) {
                if (att.type == Attachment::Image) {
                    QJsonObject imageBlock;
                    imageBlock["type"] = "image";

                    QJsonObject source;
                    source["type"] = "base64";
                    source["media_type"] = att.mimeType;
                    source["data"] = QString::fromLatin1(att.fileData.toBase64());
                    imageBlock["source"] = source;

                    contentArray.append(imageBlock);
                } else if (att.type == Attachment::Document) {
                    QJsonObject documentBlock;
                    documentBlock["type"] = "document";

                    QJsonObject source;
                    source["type"] = "base64";
                    source["media_type"] = att.mimeType;
                    source["data"] = QString::fromLatin1(att.fileData.toBase64());
                    documentBlock["source"] = source;

                    contentArray.append(documentBlock);
                }
                // TextFile 由 flattenTextAttachments 统一拼接到下方文本块
            }

            const QString fullText = flattenTextAttachments(msg.attachments) + msg.content;
            if (!fullText.trimmed().isEmpty()) {
                QJsonObject textBlock;
                textBlock["type"] = "text";
                textBlock["text"] = fullText;
                contentArray.append(textBlock);
            }

            m["content"] = contentArray;
        }

        msgArray.append(m);
    }
    body["messages"] = msgArray;

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connectReplySignals(reply);
}

QString ClaudeProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        if (delta["type"].toString() == "text_delta") {
            return delta["text"].toString();
        }
    }

    return {};
}
