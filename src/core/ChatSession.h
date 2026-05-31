#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include "Tool.h"   // ToolCall / ToolResult —— assistant 工具调用与工具结果数据结构

/**
 * @brief 附件数据结构（图片、文档或文本文件）
 *
 * type        - 附件类型：Image（图片）、Document（PDF 等文档）、TextFile（文本文件）
 * fileName    - 原始文件名
 * mimeType    - MIME 类型，如 "image/png"、"application/pdf"
 * fileData    - 文件原始字节（用于图片/文档的 base64 编码发送）
 * textContent - 文本文件的内容（仅 TextFile 类型使用）
 * relativePath - 持久化路径（v2 schema）：相对于数据根目录的路径，如
 *                 "attachments/{sessionId}/{uuid}.png"。setting 后 toJson 只
 *                 写路径不写 base64；fromJson 看到此字段时会从磁盘读 fileData。
 *                 v1 schema 的 session 不带此字段，仍走 base64 路径——加载兼容、
 *                 写回时通过 persistToDisk() 升级到 v2。
 */
struct Attachment {
    enum Type { Image, Document, TextFile };
    Type type;
    QString fileName;
    QString mimeType;
    QByteArray fileData;
    QString textContent;
    QString relativePath;

    QJsonObject toJson() const;
    static Attachment fromJson(const QJsonObject &obj, const QString &rootDataDir);

    /**
     * @brief 把 fileData 落盘到 <rootDataDir>/attachments/<sessionId>/<uuid>.<ext>，
     *        并把 relativePath 设成相应相对路径。
     *
     * 仅对 Image / Document 类型生效（TextFile 走 inline content 不落盘）。
     * 已经有 relativePath 的附件视为已迁移，跳过。fileData 为空时也跳过
     * （没有内容可写）。写盘失败不抛错，relativePath 保持为空，下次 toJson
     * 自动 fallback 到 base64 路径，旧行为兜底。
     */
    void persistToDisk(const QString &rootDataDir, const QString &sessionId);
};

/**
 * @brief 单条聊天消息的数据结构
 *
 * role        - 消息角色："user"（用户发送）或 "assistant"（AI 回复）
 * content     - 消息文本内容
 * reasoning   - assistant 的"思考链"原文（Claude thinking / OpenAI reasoning_content
 *               / DeepSeek-style 等）。可为空字符串表示没有 reasoning 块。
 * attachments - 附件列表（图片、文件等）
 * toolCalls   - assistant 本轮发起的工具调用（Claude tool_use 块）。普通消息为空。
 * toolResults - 工具执行结果（tool_result 块），下一轮随对话历史回传给模型。普通消息为空。
 * timestamp   - 消息时间戳
 * favorite    - 是否收藏
 */
struct ChatMessage {
    QString role;
    QString content;
    QString reasoning;
    QList<Attachment> attachments;
    QList<ToolCall> toolCalls;
    QList<ToolResult> toolResults;
    QDateTime timestamp;
    bool favorite = false;
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

    // --- 文件夹分组 ---
    QString folder() const;
    void setFolder(const QString &folder);

    // --- 消息管理 ---
    QList<ChatMessage> messages() const;
    ChatMessage messageAt(int index) const;         // 获取指定索引的消息
    void addMessage(const ChatMessage &msg);
    void updateLastAssistantMessage(const QString &content);
    void updateLastAssistantReasoning(const QString &reasoning);  // 把 reasoning 写到最后一条 assistant 消息
    void setMessageFavorite(int index, bool favorite);  // 切换指定消息的收藏状态
    void truncateFrom(int index);                   // 删除 index 及之后的所有消息
    void clearMessages();

    /**
     * @brief 把所有 message 的图片 / 文档附件落盘到 <rootDataDir>/attachments/<id>/...
     *
     * SessionManager::saveSession 在 toJson 之前调用此方法，让附件以文件路径
     * 而不是 base64 的形式被序列化进 session JSON，避免 session JSON 体积爆炸。
     * 已带 relativePath 的附件（v2 已迁移）跳过；fileData 为空的也跳过。
     */
    void persistAttachmentsToDisk(const QString &rootDataDir);

    // --- JSON 序列化 ---

    /**
     * @brief 将会话序列化为 JSON 对象，用于保存到文件
     * @return QJsonObject 包含 id、title、时间戳、provider、model、messages
     */
    QJsonObject toJson() const;

    /**
     * @brief 从 JSON 对象反序列化，恢复一个会话实例
     * @param obj   从 .json 文件读取的 JSON 对象
     * @param rootDataDir 数据根目录（attachments 路径相对于此目录）
     * @param parent 父对象（Qt 对象树管理内存）
     * @return 恢复的 ChatSession 指针，如果 JSON 无效返回 nullptr
     */
    static ChatSession* fromJson(const QJsonObject &obj,
                                 const QString &rootDataDir,
                                 QObject *parent = nullptr);

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
    QString m_folder;               // 所属文件夹
    QList<ChatMessage> m_messages;  // 消息历史列表

    /**
     * @brief 自动生成会话标题
     * 取第一条用户消息的前 30 个字符作为标题，超出部分用 "..." 表示
     */
    void autoGenerateTitle();
};
