#include "LLMProvider.h"
#include <QJsonDocument>
#include <QJsonObject>

LLMProvider::LLMProvider(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{}

LLMProvider::~LLMProvider()
{
    abort();
}

void LLMProvider::abort()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

void LLMProvider::connectReplySignals(QNetworkReply *reply)
{
    m_currentReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, &LLMProvider::onReadyRead);
    connect(reply, &QNetworkReply::finished, this, &LLMProvider::onReplyFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &LLMProvider::onReplyError);
}

void LLMProvider::onReadyRead()
{
    if (!m_currentReply) return;
    QByteArray newData = m_currentReply->readAll();
    QList<SSEEvent> events = m_sseParser.feed(newData);
    for (const SSEEvent &event : events) {
        if (event.eventType == "done") continue;
        if (event.data.isEmpty()) continue;
        QString token = parseSSEData(event.data);
        if (!token.isEmpty()) {
            m_accumulatedResponse += token;
            emit tokenReceived(token);
        }
    }
}

void LLMProvider::onReplyFinished()
{
    if (!m_currentReply) return;
    if (m_accumulatedResponse.isEmpty()) {
        QByteArray remaining = m_currentReply->readAll();
        if (!remaining.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(remaining);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains("error")) {
                    QString errMsg = obj["error"].toObject()["message"].toString();
                    if (errMsg.isEmpty()) errMsg = QString::fromUtf8(remaining);
                    emit errorOccurred(errMsg);
                    m_currentReply->deleteLater();
                    m_currentReply = nullptr;
                    m_sseParser.reset();
                    m_accumulatedResponse.clear();
                    return;
                }
            }
        }
    }
    emit responseFinished(m_accumulatedResponse);
    m_accumulatedResponse.clear();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    m_sseParser.reset();
}

void LLMProvider::onReplyError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error)
    if (!m_currentReply) return;
    QString errorMsg = m_currentReply->errorString();
    QByteArray body = m_currentReply->readAll();
    if (!body.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("error")) {
                QString apiMsg = obj["error"].toObject()["message"].toString();
                if (!apiMsg.isEmpty()) errorMsg = apiMsg;
            }
        }
    }
    emit errorOccurred(errorMsg);
    m_accumulatedResponse.clear();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    m_sseParser.reset();
}
