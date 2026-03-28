#include "ChatSession.h"
#include <QUuid>
#include <QJsonArray>

ChatSession::ChatSession(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_title("New Chat")
    , m_createdAt(QDateTime::currentDateTime())
    , m_updatedAt(QDateTime::currentDateTime())
{}

ChatSession::ChatSession(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_title("New Chat")
    , m_createdAt(QDateTime::currentDateTime())
    , m_updatedAt(QDateTime::currentDateTime())
{}

QString ChatSession::id() const { return m_id; }
QString ChatSession::title() const { return m_title; }

void ChatSession::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        emit titleChanged(m_title);
    }
}

QDateTime ChatSession::createdAt() const { return m_createdAt; }
QDateTime ChatSession::updatedAt() const { return m_updatedAt; }
QString ChatSession::providerName() const { return m_providerName; }
void ChatSession::setProviderName(const QString &name) { m_providerName = name; }
QString ChatSession::modelName() const { return m_modelName; }
void ChatSession::setModelName(const QString &name) { m_modelName = name; }
QList<ChatMessage> ChatSession::messages() const { return m_messages; }

void ChatSession::addMessage(const ChatMessage &msg)
{
    m_messages.append(msg);
    m_updatedAt = QDateTime::currentDateTime();
    emit messageAdded(msg);
    if (msg.role == "user" && m_messages.size() == 1) {
        autoGenerateTitle();
    }
}

void ChatSession::updateLastAssistantMessage(const QString &content)
{
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == "assistant") {
            m_messages[i].content = content;
            m_updatedAt = QDateTime::currentDateTime();
            emit lastMessageUpdated(content);
            return;
        }
    }
}

void ChatSession::clearMessages()
{
    m_messages.clear();
    m_updatedAt = QDateTime::currentDateTime();
}

void ChatSession::autoGenerateTitle()
{
    for (const auto &msg : m_messages) {
        if (msg.role == "user" && !msg.content.isEmpty()) {
            QString t = msg.content.trimmed();
            if (t.length() > 30) t = t.left(30) + "...";
            setTitle(t);
            return;
        }
    }
}

QJsonObject ChatSession::toJson() const
{
    QJsonObject obj;
    obj["id"] = m_id;
    obj["title"] = m_title;
    obj["createdAt"] = m_createdAt.toString(Qt::ISODate);
    obj["updatedAt"] = m_updatedAt.toString(Qt::ISODate);
    obj["providerName"] = m_providerName;
    obj["modelName"] = m_modelName;
    QJsonArray msgs;
    for (const auto &msg : m_messages) {
        QJsonObject m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgs.append(m);
    }
    obj["messages"] = msgs;
    return obj;
}

ChatSession* ChatSession::fromJson(const QJsonObject &obj, QObject *parent)
{
    QString id = obj["id"].toString();
    if (id.isEmpty()) return nullptr;
    auto *s = new ChatSession(id, parent);
    s->m_title = obj["title"].toString("New Chat");
    s->m_createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
    s->m_updatedAt = QDateTime::fromString(obj["updatedAt"].toString(), Qt::ISODate);
    s->m_providerName = obj["providerName"].toString();
    s->m_modelName = obj["modelName"].toString();
    for (const auto &v : obj["messages"].toArray()) {
        QJsonObject m = v.toObject();
        s->m_messages.append({m["role"].toString(), m["content"].toString()});
    }
    return s;
}
