#include "OpenAIProvider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace {

QString normalizedReasoningEffort(const QString &effort)
{
    return effort.trimmed().toLower();
}

bool supportsOpenAIReasoningEffort(const QString &model)
{
    return model.startsWith("gpt-5")
        || model.startsWith("o1")
        || model.startsWith("o3")
        || model.startsWith("o4");
}

} // namespace

OpenAIProvider::OpenAIProvider(QNetworkAccessManager *nam,
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

void OpenAIProvider::doSendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    m_accumulatedResponse.clear();
    m_sseParser.reset();

    if (m_apiKey.isEmpty()) {
        emit errorOccurred("OpenAI API key is not configured. Please set it in Settings.");
        return;
    }

    QNetworkRequest request = makeJsonRequest(QUrl(m_baseUrl + "/v1/chat/completions"));
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["stream"] = true;

    const QString reasoningEffort = normalizedReasoningEffort(m_reasoningEffort);
    if (!reasoningEffort.isEmpty()
        && reasoningEffort != "default"
        && supportsOpenAIReasoningEffort(m_model)) {
        body["reasoning_effort"] = reasoningEffort;
    }

    QJsonArray msgArray;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt;
        msgArray.append(sysMsg);
    }

    for (const auto &msg : messages) {
        QJsonObject m;
        m["role"] = msg.role;

        if (msg.attachments.isEmpty()) {
            m["content"] = msg.content;
        } else {
            QJsonArray contentArray;
            QString textParts;

            for (const auto &att : msg.attachments) {
                if (att.type == Attachment::Image) {
                    QJsonObject imageBlock;
                    imageBlock["type"] = "image_url";

                    QJsonObject imageUrl;
                    imageUrl["url"] = QStringLiteral("data:%1;base64,%2")
                        .arg(att.mimeType, QString::fromLatin1(att.fileData.toBase64()));
                    imageBlock["image_url"] = imageUrl;

                    contentArray.append(imageBlock);
                } else if (att.type == Attachment::Document) {
                    textParts += QStringLiteral("[Document: %1 (binary, not displayed)]\n\n")
                        .arg(att.fileName);
                }
                // TextFile 由 flattenTextAttachments 统一拼接到下方文本块
            }

            const QString fullText = textParts + flattenTextAttachments(msg.attachments) + msg.content;
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

LLMProviderParseResult OpenAIProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) return {};

    LLMProviderParseResult result;
    // 先把 reasoning 增量从 choices[*].delta.reasoning_content / .reasoning 里抽出来。
    // 不要在 content 不为空时跳过：同一 event 里可能 *同时* 有 content（部分代理 / 实验性 API
    // 这么发），早 return 会丢正文，导致 cleanResponse 为空 → TTS 不出声。
    for (const auto &c : choices) {
        const QJsonObject delta = c.toObject().value("delta").toObject();
        const QString rc = delta.value("reasoning_content").toString();
        if (!rc.isEmpty()) result.reasoningToken += rc;
        const QJsonValue rv = delta.value("reasoning");
        if (rv.isString()) result.reasoningToken += rv.toString();
    }

    QJsonObject delta = choices[0].toObject()["delta"].toObject();
    if (delta.contains("content")) {
        result.contentToken = delta["content"].toString();
    }

    return result;
}
