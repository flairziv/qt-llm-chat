#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

struct ChatMessage {
    QString role;
    QString content;
};

class ChatSession : public QObject
{
    Q_OBJECT
public:
    explicit ChatSession(QObject *parent = nullptr);
    explicit ChatSession(const QString &id, QObject *parent = nullptr);

    QString id() const;
    QString title() const;
    void setTitle(const QString &title);
    QDateTime createdAt() const;
    QDateTime updatedAt() const;
    QString providerName() const;
    void setProviderName(const QString &name);
    QString modelName() const;
    void setModelName(const QString &name);
    QList<ChatMessage> messages() const;
    void addMessage(const ChatMessage &msg);
    void updateLastAssistantMessage(const QString &content);
    void clearMessages();
    QJsonObject toJson() const;
    static ChatSession* fromJson(const QJsonObject &obj, QObject *parent = nullptr);

signals:
    void messageAdded(const ChatMessage &msg);
    void lastMessageUpdated(const QString &content);
    void titleChanged(const QString &title);

private:
    QString m_id;
    QString m_title;
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    QString m_providerName;
    QString m_modelName;
    QList<ChatMessage> m_messages;
    void autoGenerateTitle();
};
