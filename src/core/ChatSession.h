#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief 单条聊天消息的数据结构
 *
 * role    - 消息角色："user"（用户发送）或 "assistant"（AI 回复）
 * content - 消息文本内容
 */
struct ChatMessage {
    QString role;
    QString content;
};

/**
 * @brief 一个完整的聊天会话（对话）
 *
 * 每个 ChatSession 代表一次独立的对话，包含：
 * - 唯一 ID（UUID，用于文件存储和列表索引）
 * - 标题（自动从第一条用户消息截取前 30 个字符）
 * - 创建/更新时间戳
 * - 使用的 API 提供商和模型名称
 * - 完整的消息历史列表
 *
 * 支持 JSON 序列化/反序列化，用于持久化到磁盘文件。
 * 每个会话保存为 sessions/{uuid}.json
 */
class ChatSession : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 创建新会话，自动生成 UUID 作为 ID
     */
    explicit ChatSession(QObject *parent = nullptr);

    /**
     * @brief 使用指定 ID 创建会话（用于从 JSON 文件恢复已有会话）
     */
    explicit ChatSession(const QString &id, QObject *parent = nullptr);

    // --- 基本属性 ---
    QString id() const;
    QString title() const;
    void setTitle(const QString &title);

    QDateTime createdAt() const;
    QDateTime updatedAt() const;

    // --- API 提供商信息 ---
    QString providerName() const;       // "claude" 或 "openai"
    void setProviderName(const QString &name);
    QString modelName() const;          // 如 "claude-opus-4-6"、"gpt-4o"
    void setModelName(const QString &name);

    // --- 消息管理 ---
    QList<ChatMessage> messages() const;
    void addMessage(const ChatMessage &msg);
    void updateLastAssistantMessage(const QString &content);
    void clearMessages();

    // --- JSON 序列化 ---

    /**
     * @brief 将会话序列化为 JSON 对象，用于保存到文件
     * @return QJsonObject 包含 id、title、时间戳、provider、model、messages
     */
    QJsonObject toJson() const;

    /**
     * @brief 从 JSON 对象反序列化，恢复一个会话实例
     * @param obj   从 .json 文件读取的 JSON 对象
     * @param parent 父对象（Qt 对象树管理内存）
     * @return 恢复的 ChatSession 指针，如果 JSON 无效返回 nullptr
     */
    static ChatSession* fromJson(const QJsonObject &obj, QObject *parent = nullptr);

signals:
    void messageAdded(const ChatMessage &msg);        // 新消息添加时发射
    void lastMessageUpdated(const QString &content);  // 最后一条 assistant 消息更新时发射
    void titleChanged(const QString &title);          // 标题变化时发射（通知 SessionListWidget 刷新显示）

private:
    QString m_id;                   // 会话唯一标识（UUID）
    QString m_title;                // 会话标题（显示在左侧列表）
    QDateTime m_createdAt;          // 创建时间
    QDateTime m_updatedAt;          // 最后更新时间
    QString m_providerName;         // API 提供商名称
    QString m_modelName;            // 模型名称
    QList<ChatMessage> m_messages;  // 消息历史列表

    /**
     * @brief 自动生成会话标题
     * 取第一条用户消息的前 30 个字符作为标题，超出部分用 "..." 表示
     */
    void autoGenerateTitle();
};
