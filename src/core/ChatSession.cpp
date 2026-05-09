#include "ChatSession.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QJsonDocument>

// ============================================================
// Attachment 序列化
// ============================================================

/**
 * @brief 将附件序列化为 JSON 对象
 *
 * 三种写法：
 *   - TextFile：写 textContent（纯文本，inline）
 *   - Image / Document with relativePath：v2 schema，只写路径，文件留在磁盘上
 *   - Image / Document without relativePath：v1 schema 兼容，base64 写 fileData
 *
 * 调用前 SessionManager 通常已经走 ChatSession::persistAttachmentsToDisk()
 * 把 fileData 落盘并设置 relativePath，所以新写出的 session JSON 都是 v2 形态。
 * 但如果 persistToDisk 写盘失败（磁盘满 / 权限），relativePath 留空，这里
 * fallback 到 base64 路径，不丢数据。
 */
QJsonObject Attachment::toJson() const
{
    QJsonObject obj;
    obj["type"] = static_cast<int>(type);
    obj["fileName"] = fileName;
    obj["mimeType"] = mimeType;
    if (type == TextFile) {
        obj["textContent"] = textContent;
    } else if (!relativePath.isEmpty()) {
        // v2: 路径模式，session JSON 只引用文件
        obj["filePath"] = relativePath;
    } else {
        // v1 兜底：仍然 base64
        obj["fileData"] = QString::fromLatin1(fileData.toBase64());
    }
    return obj;
}

/**
 * @brief 从 JSON 对象反序列化附件
 *
 * - filePath 字段存在 → v2 schema，从 <rootDataDir>/<filePath> 读取字节填进 fileData
 * - 否则有 fileData 字段 → v1 schema，base64 解码（旧 session 的兼容路径）
 *
 * 文件丢失（磁盘上的图片被外部删除）：fileData 留空，UI 自然显示空气泡，
 * 不抛错也不清除附件元数据，方便后续手动恢复。
 */
Attachment Attachment::fromJson(const QJsonObject &obj, const QString &rootDataDir)
{
    Attachment a;
    a.type = static_cast<Attachment::Type>(obj["type"].toInt());
    a.fileName = obj["fileName"].toString();
    a.mimeType = obj["mimeType"].toString();
    if (a.type == Attachment::TextFile) {
        a.textContent = obj["textContent"].toString();
    } else if (obj.contains("filePath")) {
        a.relativePath = obj["filePath"].toString();
        if (!rootDataDir.isEmpty() && !a.relativePath.isEmpty()) {
            QFile f(rootDataDir + "/" + a.relativePath);
            if (f.open(QIODevice::ReadOnly)) {
                a.fileData = f.readAll();
                f.close();
            }
        }
    } else if (obj.contains("localPath")) {
        // 旧 schema 兼容：localPath 路径格式为 "sid/uuid.ext"，需补 "attachments/" 前缀
        a.relativePath = QStringLiteral("attachments/") + obj["localPath"].toString();
        if (!rootDataDir.isEmpty() && !a.relativePath.isEmpty()) {
            QFile f(rootDataDir + "/" + a.relativePath);
            if (f.open(QIODevice::ReadOnly)) {
                a.fileData = f.readAll();
                f.close();
            }
        }
    } else {
        // v1: base64 字符串还原为原始字节
        a.fileData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    }
    return a;
}

/**
 * @brief 把附件 fileData 写盘到 <rootDataDir>/attachments/<sessionId>/<uuid>.<ext>
 *
 * 仅对 Image / Document 生效；TextFile 没有外部文件需求。已经有 relativePath
 * 视为已迁移直接跳过。文件名后缀通过 Attachment::fileName 推断，没有则按 mimeType
 * 兜底（image/png → png 等）。
 */
void Attachment::persistToDisk(const QString &rootDataDir, const QString &sessionId)
{
    if (type == TextFile) return;
    if (!relativePath.isEmpty()) return;
    if (fileData.isEmpty()) return;
    if (rootDataDir.isEmpty() || sessionId.isEmpty()) return;

    // 后缀优先取 fileName 自身，找不到再按 mimeType 兜底
    QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext.isEmpty()) {
        if (mimeType.startsWith("image/png")) ext = "png";
        else if (mimeType.startsWith("image/jpeg")) ext = "jpg";
        else if (mimeType.startsWith("image/webp")) ext = "webp";
        else if (mimeType.startsWith("image/gif")) ext = "gif";
        else if (mimeType.startsWith("application/pdf")) ext = "pdf";
        else ext = "bin";
    }

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString relPath = QStringLiteral("attachments/%1/%2.%3").arg(sessionId, uuid, ext);
    const QString absPath = rootDataDir + "/" + relPath;
    QDir().mkpath(QFileInfo(absPath).absolutePath());

    QFile f(absPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(fileData);
        f.close();
        relativePath = relPath;
    }
    // 写盘失败保持 relativePath 空，下次 toJson 自动走 base64 兜底
}

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
QString ChatSession::folder() const { return m_folder; }
void ChatSession::setFolder(const QString &folder) { m_folder = folder; }

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
    ChatMessage stored = msg;
    if (!stored.timestamp.isValid()) {
        stored.timestamp = QDateTime::currentDateTime();
    }
    m_messages.append(stored);
    m_updatedAt = QDateTime::currentDateTime();
    emit messageAdded(stored);

    // 第一条用户消息 → 自动生成标题
    if (stored.role == "user" && m_messages.size() == 1) {
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
 * @brief 把 reasoning 字段写到最后一条 assistant 消息
 *
 * 与 updateLastAssistantMessage 配套：流式结束后调用，避免每个 reasoning
 * token 都触发一次 setMessageReasoning 引发的写盘。reasoning 字段不发射
 * lastMessageUpdated 信号——它是另一个独立的 UI 通道（折叠区块），
 * 由 MainWindow 直接转发到 MessageBubble。
 */
void ChatSession::updateLastAssistantReasoning(const QString &reasoning)
{
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == "assistant") {
            m_messages[i].reasoning = reasoning;
            m_updatedAt = QDateTime::currentDateTime();
            return;
        }
    }
}

/**
 * @brief 把每条消息的图片 / 文档附件迁移到 v2 schema（写盘 + 设置 relativePath）
 */
void ChatSession::persistAttachmentsToDisk(const QString &rootDataDir)
{
    for (auto &msg : m_messages) {
        for (auto &att : msg.attachments) {
            att.persistToDisk(rootDataDir, m_id);
        }
    }
    // 不更新 m_updatedAt：迁移是技术性的写盘动作，不算业务变更
}

/**
 * @brief 清空所有消息（不删除会话本身）
 */
void ChatSession::clearMessages()
{
    m_messages.clear();
    m_updatedAt = QDateTime::currentDateTime();
}

/**
 * @brief 获取指定索引的消息
 * @param index 消息在列表中的索引（从 0 开始）
 * @return 对应的 ChatMessage，索引越界时返回空消息
 */
ChatMessage ChatSession::messageAt(int index) const
{
    if (index >= 0 && index < m_messages.size()) {
        return m_messages.at(index);
    }
    return ChatMessage();
}

/**
 * @brief 更新指定消息的收藏状态
 */
void ChatSession::setMessageFavorite(int index, bool favorite)
{
    if (index < 0 || index >= m_messages.size()) return;
    m_messages[index].favorite = favorite;
    m_updatedAt = QDateTime::currentDateTime();
}

/**
 * @brief 删除指定索引及之后的所有消息（用于编辑/撤回）
 *
 * 例如消息列表为 [user, assistant, user, assistant]，
 * truncateFrom(2) 后变为 [user, assistant]。
 *
 * @param index 起始删除位置（该位置的消息也会被删除）
 */
void ChatSession::truncateFrom(int index)
{
    if (index < 0 || index >= m_messages.size()) return;
    while (m_messages.size() > index) {
        m_messages.removeLast();
    }
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
    obj["folder"] = m_folder;

    QJsonArray msgArray;
    for (const auto &msg : m_messages) {
        QJsonObject m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (!msg.reasoning.isEmpty()) {
            m["reasoning"] = msg.reasoning;
        }
        if (msg.timestamp.isValid()) {
            m["timestamp"] = msg.timestamp.toString(Qt::ISODate);
        }
        if (msg.favorite) {
            m["favorite"] = true;
        }
        if (!msg.attachments.isEmpty()) {
            QJsonArray attArray;
            for (const auto &att : msg.attachments) {
                attArray.append(att.toJson());
            }
            m["attachments"] = attArray;
        }
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
 * @param rootDataDir 数据根目录（attachment 路径相对于此目录）
 * @param parent Qt 父对象，用于自动内存管理
 * @return 恢复的 ChatSession*，JSON 无效（缺少 id）时返回 nullptr
 */
ChatSession* ChatSession::fromJson(const QJsonObject &obj,
                                   const QString &rootDataDir,
                                   QObject *parent)
{
    QString id = obj["id"].toString();
    if (id.isEmpty()) return nullptr;

    auto *session = new ChatSession(id, parent);
    session->m_title = obj["title"].toString("New Chat");
    session->m_createdAt = QDateTime::fromString(obj["created_at"].toString(), Qt::ISODate);
    session->m_updatedAt = QDateTime::fromString(obj["updated_at"].toString(), Qt::ISODate);
    session->m_providerName = obj["provider"].toString();
    session->m_modelName = obj["model"].toString();
    session->m_folder = obj["folder"].toString();

    // 逐条恢复消息列表（直接写入 m_messages，不触发信号）
    QJsonArray msgArray = obj["messages"].toArray();
    for (const auto &val : msgArray) {
        QJsonObject m = val.toObject();
        ChatMessage msg;
        msg.role = m["role"].toString();
        msg.content = m["content"].toString();
        msg.reasoning = m["reasoning"].toString();
        if (m.contains("timestamp")) {
            msg.timestamp = QDateTime::fromString(m["timestamp"].toString(), Qt::ISODate);
        }
        msg.favorite = m["favorite"].toBool(false);
        if (m.contains("attachments")) {
            QJsonArray attArray = m["attachments"].toArray();
            for (const auto &attVal : attArray) {
                msg.attachments.append(Attachment::fromJson(attVal.toObject(), rootDataDir));
            }
        }
        session->m_messages.append(msg);
    }
    return session;
}
