#include "SessionManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>

// ============================================================
// 构造函数
// ============================================================

/**
 * @brief 初始化会话管理器
 *
 * 将数据根目录拼接上 "/sessions" 作为会话文件目录，
 * 并确保该目录存在（不存在则递归创建）。
 *
 * @param dataDir 数据根目录，如 "Downloads/LLMChat"
 * @param parent  Qt 父对象
 *
 * 最终 m_dataDir = "Downloads/LLMChat/sessions"
 */
SessionManager::SessionManager(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir + "/sessions")
{
    QDir dir;
    dir.mkpath(m_dataDir);
}

// ============================================================
// 会话生命周期管理
// ============================================================

/**
 * @brief 创建新会话
 *
 * 流程：
 * 1. new ChatSession(this) — 以 SessionManager 为父对象，确保自动内存管理
 * 2. prepend 到列表头部 — 新会话排在最前面（最近优先）
 * 3. 设为激活会话
 * 4. 发射 sessionCreated 信号 → ChatPage::addSession() 更新左侧列表
 * 5. 发射 activeSessionChanged 信号 → 切换聊天区域
 *
 * @return 新创建的 ChatSession 指针
 */
ChatSession* SessionManager::createSession()
{
    auto *session = new ChatSession(this);
    m_sessions.prepend(session);
    m_activeSession = session;
    emit sessionCreated(session);
    emit activeSessionChanged(session);
    return session;
}

/**
 * @brief 根据 UUID 查找会话
 *
 * 线性遍历会话列表，匹配 ID。
 * 会话数量通常不大（几十到几百），线性查找足够高效。
 *
 * @param id 会话 UUID
 * @return 匹配的会话指针，未找到返回 nullptr
 */
ChatSession* SessionManager::session(const QString &id) const
{
    for (auto *s : m_sessions) {
        if (s->id() == id) return s;
    }
    return nullptr;
}

/// 返回所有会话的列表副本
QList<ChatSession*> SessionManager::allSessions() const
{
    return m_sessions;
}

/**
 * @brief 删除指定会话
 *
 * 完整流程：
 * 1. 遍历列表找到目标会话，使用 takeAt() 从列表中移除（不释放内存）
 * 2. 删除磁盘上的 JSON 文件（QFile::remove）
 * 3. 如果删除的是当前激活会话：
 *    - 列表非空 → 切换到第一个会话
 *    - 列表为空 → 激活会话设为 nullptr
 *    - 发射 activeSessionChanged 通知 UI 更新
 * 4. deleteLater() 安全释放内存（等待当前事件循环完成）
 * 5. 发射 sessionDeleted 信号通知 UI 移除列表项
 *
 * @param id 要删除的会话 UUID
 */
void SessionManager::deleteSession(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i]->id() == id) {
            ChatSession *s = m_sessions.takeAt(i);

            // 删除磁盘文件：sessions/{uuid}.json
            QFile::remove(sessionFilePath(id));

            // 如果删除的是激活会话，需要切换到其他会话
            if (m_activeSession == s) {
                m_activeSession = m_sessions.isEmpty() ? nullptr : m_sessions.first();
                emit activeSessionChanged(m_activeSession);
            }

            // deleteLater 而非 delete：确保在事件循环中安全释放
            // （可能还有信号槽连接或延迟调用引用此对象）
            s->deleteLater();
            emit sessionDeleted(id);
            return;
        }
    }
}

// ============================================================
// 持久化 —— 保存 / 加载
// ============================================================

/**
 * @brief 将单个会话保存到 JSON 文件
 *
 * 调用时机：MainWindow::onResponseFinished() — AI 回复完成后自动保存
 *
 * 写入路径：sessions/{uuid}.json
 * 文件格式：缩进的 JSON（QJsonDocument::Indented），便于调试查看
 *
 * 写入模式：WriteOnly | Truncate — 清空文件后重新写入全部内容
 * （不是追加模式，每次保存都是完整覆盖，确保数据一致性）
 *
 * @param session 要保存的会话，nullptr 时直接返回
 */
void SessionManager::saveSession(ChatSession *session)
{
    if (!session) return;
    QString path = sessionFilePath(session->id());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("Failed to save session: %s", qPrintable(path));
        return;
    }
    // ChatSession::toJson() 返回包含 id、title、时间戳、消息列表的 QJsonObject
    QJsonDocument doc(session->toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

/**
 * @brief 从磁盘加载所有历史会话
 *
 * 调用时机：MainWindow 构造时调用，恢复上次关闭前的所有会话
 *
 * 流程：
 * 1. 扫描 sessions/ 目录下所有 *.json 文件（按文件修改时间排序）
 * 2. 逐个打开文件 → 解析 JSON → ChatSession::fromJson() 反序列化
 * 3. 无效文件（打开失败或 JSON 格式错误）会被静默跳过
 * 4. 按 updatedAt 降序排列（最近更新的在前）
 * 5. 将列表第一个（最新）会话设为激活会话
 *
 * 注意：此方法不发射 sessionCreated 信号，
 * MainWindow 在调用后会手动遍历列表初始化 UI。
 */
void SessionManager::loadAllSessions()
{
    QDir dir(m_dataDir);
    QStringList filters;
    filters << "*.json";
    // QDir::Time 按文件修改时间排序（仅作初步排序，后续会用 updatedAt 重新排序）
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    for (const QFileInfo &fi : files) {
        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        // 跳过无效 JSON 文件（损坏或格式错误）
        if (doc.isNull()) continue;

        // fromJson 内部会校验 id 字段，无效时返回 nullptr
        ChatSession *s = ChatSession::fromJson(doc.object(), this);
        if (s) {
            m_sessions.append(s);
        }
    }

    // 按会话的 updatedAt 时间降序排列，最近更新的会话排在前面
    // 这样左侧会话列表中，最近活跃的对话显示在顶部
    std::sort(m_sessions.begin(), m_sessions.end(),
        [](const ChatSession *a, const ChatSession *b) {
            return a->updatedAt() > b->updatedAt();
        });

    // 默认激活最新的会话
    if (!m_sessions.isEmpty()) {
        m_activeSession = m_sessions.first();
    }
}

// ============================================================
// 激活会话管理
// ============================================================

/// 返回当前激活的会话，无会话时为 nullptr
ChatSession* SessionManager::activeSession() const { return m_activeSession; }

/**
 * @brief 切换激活会话
 *
 * 用户在左侧列表点击某个会话时，ChatPage 发射 sessionSelected(id)，
 * MainWindow 接收后调用此方法切换激活会话。
 *
 * 仅在目标会话存在且不同于当前激活会话时才切换，
 * 避免重复切换导致不必要的 UI 刷新。
 *
 * @param id 目标会话的 UUID
 */
void SessionManager::setActiveSession(const QString &id)
{
    ChatSession *s = session(id);
    if (s && s != m_activeSession) {
        m_activeSession = s;
        emit activeSessionChanged(s);
    }
}

// ============================================================
// 工具方法
// ============================================================

/**
 * @brief 根据会话 UUID 生成磁盘文件路径
 *
 * 格式：{m_dataDir}/{id}.json
 * 示例：Downloads/LLMChat/sessions/a3f5e2b1-4c8d-4a2e-9f1b-7d6c3e8a5b4f.json
 *
 * @param id 会话 UUID（不带花括号）
 * @return 完整文件路径
 */
QString SessionManager::sessionFilePath(const QString &id) const
{
    return m_dataDir + "/" + id + ".json";
}
