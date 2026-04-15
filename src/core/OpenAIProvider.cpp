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

void OpenAIProvider::sendStreamingRequest(
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

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
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
        m["content"] = msg.content;
        msgArray.append(m);
    }
    body["messages"] = msgArray;

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connectReplySignals(reply);
}

QString OpenAIProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) return {};

    QJsonObject delta = choices[0].toObject()["delta"].toObject();
    if (delta.contains("content")) {
        return delta["content"].toString();
    }

    return {};
}