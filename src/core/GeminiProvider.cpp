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

void GeminiProvider::sendStreamingRequest(
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

    const QString url = QStringLiteral("%1/v1beta/models/%2:streamGenerateContent?alt=sse&key=%3")
                            .arg(m_baseUrl, m_model, m_apiKey);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QJsonObject body;

    QJsonObject generationConfig;
    generationConfig["maxOutputTokens"] = maxTokens;
    generationConfig["temperature"] = temperature;
    body["generationConfig"] = generationConfig;

    if (!systemPrompt.isEmpty()) {
        QJsonObject systemInstruction;
        QJsonArray systemParts;
        QJsonObject systemPart;
        systemPart["text"] = systemPrompt;
        systemParts.append(systemPart);
        systemInstruction["parts"] = systemParts;
        body["systemInstruction"] = systemInstruction;
    }

    QJsonArray contents;
    for (const auto &msg : messages) {
        if (msg.role == "system") continue;

        QJsonObject content;
        content["role"] = (msg.role == "assistant") ? QStringLiteral("model")
                                                    : QStringLiteral("user");

        QJsonArray parts;

        for (const auto &att : msg.attachments) {
            if (att.type == Attachment::Image) {
                QJsonObject inlinePart;
                QJsonObject inlineData;
                inlineData["mimeType"] = att.mimeType;
                inlineData["data"] = QString::fromLatin1(att.fileData.toBase64());
                inlinePart["inlineData"] = inlineData;
                parts.append(inlinePart);
            }
        }

        QString textPart;
        for (const auto &att : msg.attachments) {
            if (att.type == Attachment::TextFile) {
                textPart += QStringLiteral("[File: %1]\n%2\n\n").arg(att.fileName, att.textContent);
            } else if (att.type == Attachment::Document) {
                textPart += QStringLiteral("[Document: %1 (binary, not supported)]\n\n").arg(att.fileName);
            }
        }
        textPart += msg.content;

        if (!textPart.trimmed().isEmpty()) {
            QJsonObject textObject;
            textObject["text"] = textPart;
            parts.append(textObject);
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

QString GeminiProvider::parseSSEData(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    const QJsonArray candidates = doc.object().value("candidates").toArray();
    if (candidates.isEmpty()) return {};

    const QJsonObject content = candidates[0].toObject().value("content").toObject();
    const QJsonArray parts = content.value("parts").toArray();
    if (parts.isEmpty()) return {};

    return parts[0].toObject().value("text").toString();
}
