#pragma once
#include <QListWidget>

class ChatSession;

/**
 * @brief 会话列表控件 —— 显示所有聊天会话的侧栏列表
 *
 * 继承自 QListWidget，位于 ChatPage 左侧面板中。
 * 每个列表项（QListWidgetItem）对应一个 ChatSession：
 *   - 显示文本：会话标题（session->title()）
 *   - UserRole 数据：会话 ID（用于唯一标识）
 *   - ToolTip：最后更新时间
 *
 * 交互方式：
 *   - 单击选中会话 → 发射 sessionSelected 信号
 *   - 右键菜单删除 → 发射 sessionDeleteRequested 信号
 *
 * 在 ChatPage 左侧面板中的位置：
 *   ┌─────────────────┐
 *   │ [Claude ▼][+New] │
 *   ├─────────────────┤
 *   │ ● Session 1     │ ← 选中状态（高亮）
 *   │   Session 2     │
 *   │   Session 3     │ ← 右键弹出 "Delete" 菜单
 *   │                 │
 *   │   (stretch)     │
 *   └─────────────────┘
 */
class SessionListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit SessionListWidget(QWidget *parent = nullptr);

    void addSession(ChatSession *session);     // 添加一个会话到列表顶部
    void removeSession(const QString &id);     // 按 ID 移除指定会话
    void setActiveSession(const QString &id);  // 按 ID 高亮选中指定会话
    void refreshList(const QList<ChatSession*> &sessions);  // 清空并重新填充整个列表

signals:
    void sessionSelected(const QString &id);         // 用户点击选中某个会话
    void sessionDeleteRequested(const QString &id);   // 用户通过右键菜单请求删除会话

private slots:
    void onItemClicked(QListWidgetItem *item);  // 列表项点击处理
    void onContextMenu(const QPoint &pos);      // 右键上下文菜单处理

private:
    void setupUI();  // 初始化列表样式和信号连接
};
