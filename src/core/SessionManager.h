#pragma once
#include <QObject>
#include <QList>
#include "ChatSession.h"

/**
 * @brief 多会话管理器
 *
 * 负责管理所有 ChatSession 的生命周期，包括：
 * - 创建 / 删除会话
 * - 按 ID 查找会话
 * - 维护「当前激活会话」指针
 * - 会话数据的磁盘持久化（JSON 文件读写）
 *
 * 存储结构：
 *   Downloads/LLMChat/sessions/
 *   ├── {uuid1}.json     ← 每个会话一个文件
 *   ├── {uuid2}.json
 *   └── ...
 *
 * 信号连接关系（由 MainWindow 建立）：
 *   sessionCreated      → ChatPage::addSession          添加到左侧列表
 *   sessionDeleted      → ChatPage::removeSession        从列表移除
 *   activeSessionChanged → MainWindow::onSessionSelected  切换聊天区域显示
 *
 * 所有权：
 *   SessionManager 是所有 ChatSession 的 Qt 父对象，
 *   ChatSession 通过 new ChatSession(this) 创建，
 *   SessionManager 销毁时会自动释放所有会话。
 */
class SessionManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param dataDir 数据根目录（如 Downloads/LLMChat），会在其下创建 sessions/ 子目录
     * @param parent  Qt 父对象
     */
    explicit SessionManager(const QString &dataDir, QObject *parent = nullptr);

    // --- 会话生命周期管理 ---

    /**
     * @brief 创建一个新会话
     *
     * 自动生成 UUID、插入到列表头部、设为激活会话，
     * 并发射 sessionCreated 和 activeSessionChanged 信号。
     * @return 新创建的 ChatSession 指针
     */
    ChatSession* createSession();

    /**
     * @brief 根据 UUID 查找会话
     * @param id 会话的唯一标识
     * @return 找到返回指针，未找到返回 nullptr
     */
    ChatSession* session(const QString &id) const;

    /**
     * @brief 获取所有会话列表
     * @return 会话指针列表，按最近更新时间降序排列
     */
    QList<ChatSession*> allSessions() const;

    /**
     * @brief 删除指定会话
     *
     * 执行以下操作：
     * 1. 从内存列表中移除
     * 2. 删除磁盘上的 JSON 文件
     * 3. 如果删除的是激活会话，自动切换到列表中的第一个会话
     * 4. 调用 deleteLater() 安全释放内存
     * 5. 发射 sessionDeleted 信号
     *
     * @param id 要删除的会话 UUID
     */
    void deleteSession(const QString &id);

    // --- 持久化 ---

    /**
     * @brief 将单个会话保存到磁盘
     *
     * 序列化为 JSON 格式，写入 sessions/{uuid}.json。
     * 使用 QJsonDocument::Indented 格式化输出，便于人工查看。
     * 被 MainWindow::onResponseFinished() 在 AI 回复完成后调用。
     *
     * @param session 要保存的会话指针
     */
    void saveSession(ChatSession *session);

    /**
     * @brief 从磁盘加载所有历史会话
     *
     * 扫描 sessions/ 目录下的所有 .json 文件，
     * 逐个反序列化为 ChatSession 对象，按 updatedAt 降序排列。
     * 自动将最新的会话设为激活会话。
     * 在应用启动时由 MainWindow 调用。
     */
    void loadAllSessions();

    // --- 激活会话管理 ---

    /**
     * @brief 获取当前激活的会话
     * @return 激活会话指针，无会话时返回 nullptr
     */
    ChatSession* activeSession() const;

    /**
     * @brief 切换激活会话
     *
     * 根据 ID 查找目标会话，如果找到且不同于当前激活会话，
     * 则更新指针并发射 activeSessionChanged 信号。
     *
     * @param id 目标会话的 UUID
     */
    void setActiveSession(const QString &id);

signals:
    void sessionCreated(ChatSession *session);        // 新会话创建时发射
    void sessionDeleted(const QString &id);           // 会话删除时发射
    void activeSessionChanged(ChatSession *session);  // 激活会话切换时发射（session 可能为 nullptr）

private:
    QString m_dataDir;                  // 会话文件存储目录（dataDir/sessions/）
    QList<ChatSession*> m_sessions;     // 所有会话的内存列表
    ChatSession *m_activeSession = nullptr;  // 当前激活的会话

    /**
     * @brief 根据会话 ID 生成文件路径
     * @param id 会话 UUID
     * @return 完整路径，如 "Downloads/LLMChat/sessions/{uuid}.json"
     */
    QString sessionFilePath(const QString &id) const;
};
