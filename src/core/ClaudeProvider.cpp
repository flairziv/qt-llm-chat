#include "ClaudeProvider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ClaudeProvider::ClaudeProvider(QNetworkAccessManager *nam,
                               const QString &apiKey,
                               const QString &baseUrl,
                               const QString &model,
                               QObject *parent)
    : LLMProvider(nam, parent)
    , m_apiKey(apiKey)
    , m_baseUrl(baseUrl)
    , m_model(model)
{}

void ClaudeProvider::sendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    m_accumulatedResponse.clear();
    m_sseParser.reset();
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("Claude API key is not configured.");
        return;
    }
    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/messages"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["stream"] = true;
    if (!systemPrompt.isEmpty()) body["system"] = systemPrompt;
    QJsonArray msgArray;
    for (const auto &msg : messages) {
        if (msg.role == "system") continue;
        QJsonObject m;
        m["role"] = msg.role;
        m["content"] = msg.content;
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
    if (obj["type"].toString() == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        if (delta["type"].toString() == "text_delta")
            return delta["text"].toString();
    }
    return {};
}
