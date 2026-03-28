#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QList>
#include "ChatSession.h"
#include "SSEParser.h"

class LLMProvider : public QObject
{
    Q_OBJECT
public:
    explicit LLMProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);
    virtual ~LLMProvider();

    virtual void sendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) = 0;

    virtual void abort();
    virtual QString providerName() const = 0;

signals:
    void tokenReceived(const QString &token);
    void responseFinished(const QString &fullResponse);
    void errorOccurred(const QString &errorMessage);

protected:
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_currentReply = nullptr;
    QString m_accumulatedResponse;
    SSEParser m_sseParser;
    void connectReplySignals(QNetworkReply *reply);
    virtual QString parseSSEData(const QByteArray &data) = 0;

private slots:
    void onReadyRead();
    void onReplyFinished();
    void onReplyError(QNetworkReply::NetworkError error);
};
