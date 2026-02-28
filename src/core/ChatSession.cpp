#include "ChatSession.h"
#include <QUuid>

// ============================================================
// 构造函数
// ============================================================

/**
 * @brief 创建新会话
 *
 * 自动生成一个不带花括号的 UUID 作为唯一 ID
 * 例如："a3f5e2b1-4c8d-4a2e-9f1b-7d6c3e8a5b4f"
 * 默认标题为 "New Chat"，后续会由 autoGenerateTitle() 自动更新
 */
ChatSession::ChatSession(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_title("New Chat")
    , m_createdAt(QDateTime::currentDateTime())
    , m_updatedAt(QDateTime::currentDateTime())
{
}

/**
 * @brief 使用已有 ID 创建会话（从 JSON 文件恢复时使用）
 */
ChatSession::ChatSession(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_title("New Chat")
    , m_createdAt(QDateTime::currentDateTime())
    , m_updatedAt(QDateTime::currentDateTime())
{
}

// ============================================================
// 基本属性的 getter / setter
// ============================================================

QString ChatSession::id() const { return m_id; }
QString ChatSession::title() const { return m_title; }

/**
 * @brief 设置会话标题，标题变化时发射 titleChanged 信号
 *
 * SessionListWidget 连接了这个信号，会自动更新列表中对应项的显示文本
 */
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

// ============================================================
// 消息管理
// ============================================================

QList<ChatMessage> ChatSession::messages() const { return m_messages; }

/**
 * @brief 添加一条消息到会话末尾
 *
 * 同时更新时间戳，并发射 messageAdded 信号。
 * 如果这是第一条用户消息，会自动调用 autoGenerateTitle()
 * 用消息内容前 30 个字符作为会话标题。
 */
void ChatSession::addMessage(const ChatMessage &msg)
{
    m_messages.append(msg);
    m_updatedAt = QDateTime::currentDateTime();
    emit messageAdded(msg);

    // 第一条用户消息 → 自动生成标题
    if (msg.role == "user" && m_messages.size() == 1) {
        autoGenerateTitle();
    }
}

/**
 * @brief 更新最后一条 assistant 消息的内容
 *
 * 用于 SSE 流式接收完成后，将完整回复内容回写到消息列表。
 * 从消息列表末尾向前查找第一条 role == "assistant" 的消息进行更新。
 */
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

/**
 * @brief 清空所有消息（不删除会话本身）
 */
void ChatSession::clearMessages()
{
    m_messages.clear();
    m_updatedAt = QDateTime::currentDateTime();
}

// ============================================================
// 标题自动生成
// ============================================================

/**
 * @brief 从第一条用户消息中截取前 30 个字符作为会话标题
 *
 * 超过 30 个字符时追加 "..."
 * 例如用户输入 "请帮我写一个冒泡排序的 Python 实现并解释原理"
 * → 标题变为 "请帮我写一个冒泡排序的 Python 实现并解释原..."
 */
void ChatSession::autoGenerateTitle()
{
    for (const auto &msg : m_messages) {
        if (msg.role == "user" && !msg.content.isEmpty()) {
            QString t = msg.content.left(30);
            if (msg.content.length() > 30) t += "...";
            setTitle(t);
            return;
        }
    }
}

// ============================================================
// JSON 序列化 / 反序列化
// ============================================================

/**
 * @brief 将整个会话序列化为 JSON 对象
 *
 * 输出格式：
 * {
 *   "id": "uuid-string",
 *   "title": "会话标题",
 *   "created_at": "2025-01-15T10:30:00",
 *   "updated_at": "2025-01-15T10:35:00",
 *   "provider": "claude",
 *   "model": "claude-opus-4-6",
 *   "messages": [
 *     {"role": "user", "content": "你好"},
 *     {"role": "assistant", "content": "你好！有什么可以帮你的吗？"}
 *   ]
 * }
 *
 * 被 SessionManager::saveSession() 调用，写入 sessions/{uuid}.json
 */
QJsonObject ChatSession::toJson() const
{
    QJsonObject obj;
    obj["id"] = m_id;
    obj["title"] = m_title;
    obj["created_at"] = m_createdAt.toString(Qt::ISODate);
    obj["updated_at"] = m_updatedAt.toString(Qt::ISODate);
    obj["provider"] = m_providerName;
    obj["model"] = m_modelName;

    QJsonArray msgArray;
    for (const auto &msg : m_messages) {
        QJsonObject m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgArray.append(m);
    }
    obj["messages"] = msgArray;
    return obj;
}

/**
 * @brief 从 JSON 对象恢复会话实例（反序列化）
 *
 * 被 SessionManager::loadAllSessions() 调用，
 * 从 sessions/ 目录的每个 .json 文件中读取并恢复会话。
 *
 * @param obj    从文件读取的 JSON 对象
 * @param parent Qt 父对象，用于自动内存管理
 * @return 恢复的 ChatSession*，JSON 无效（缺少 id）时返回 nullptr
 */
ChatSession* ChatSession::fromJson(const QJsonObject &obj, QObject *parent)
{
    QString id = obj["id"].toString();
    if (id.isEmpty()) return nullptr;

    auto *session = new ChatSession(id, parent);
    session->m_title = obj["title"].toString("New Chat");
    session->m_createdAt = QDateTime::fromString(obj["created_at"].toString(), Qt::ISODate);
    session->m_updatedAt = QDateTime::fromString(obj["updated_at"].toString(), Qt::ISODate);
    session->m_providerName = obj["provider"].toString();
    session->m_modelName = obj["model"].toString();

    // 逐条恢复消息列表（直接写入 m_messages，不触发信号）
    QJsonArray msgArray = obj["messages"].toArray();
    for (const auto &val : msgArray) {
        QJsonObject m = val.toObject();
        ChatMessage msg;
        msg.role = m["role"].toString();
        msg.content = m["content"].toString();
        session->m_messages.append(msg);
    }
    return session;
}
