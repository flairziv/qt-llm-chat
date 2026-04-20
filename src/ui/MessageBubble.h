#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

/**
 * @brief 消息气泡自定义控件
 *
 * 每条聊天消息对应一个 MessageBubble 实例，根据角色（User / Assistant）
 * 决定对齐方式和样式：
 *
 *   User 气泡（右对齐）：
 *   ┌─────────────────────────────────────────┐
 *   │                                    You  │
 *   │                        ┌────────────┐   │
 *   │                        │ 用户消息   │   │
 *   │                        └────────────┘   │
 *   └─────────────────────────────────────────┘
 *
 *   Assistant 气泡（左对齐）：
 *   ┌─────────────────────────────────────────┐
 *   │  Assistant                              │
 *   │  ┌────────────┐                         │
 *   │  │ 助手回复   │                         │
 *   │  └────────────┘                         │
 *   └─────────────────────────────────────────┘
 *
 * 布局结构：
 *   outerLayout (QVBoxLayout)
 *   ├── m_roleLabel        ← "You" 或 "Assistant"，粗体
 *   └── bubbleRow (QHBoxLayout)
 *       ├── <stretch>      ← User 时在左侧（右对齐），Assistant 时在右侧（左对齐）
 *       └── m_bubbleWidget ← 气泡容器（最大宽度 560px）
 *           └── m_contentLabel ← 消息文本，支持自动换行和鼠标选中
 */
class MessageBubble : public QWidget
{
    Q_OBJECT
public:
    /** @brief 消息角色枚举 */
    enum Role { User, Assistant };

    /**
     * @brief 构造函数
     * @param role     消息角色（决定对齐方式和样式）
     * @param content  消息文本内容
     * @param parent   父 widget
     */
    explicit MessageBubble(Role role, const QString &content,
                           QWidget *parent = nullptr,
                           const QString &userName = "You",
                           const QString &assistantName = "Assistant");

    void appendText(const QString &text);   // 追加文本（用于流式 token 填充）
    void setContent(const QString &content); // 替换全部文本内容
    void setRoleName(const QString &name);  // 更新角色标签显示名
    QString content() const;                 // 获取当前文本内容
    Role role() const;                       // 获取消息角色

private:
    void setupUI();  // 根据角色构建不同对齐方式的布局

    Role m_role;                // 消息角色
    QString m_content;          // 消息文本内容
    QString m_userName;         // 用户显示名
    QString m_assistantName;    // 助手显示名
    QLabel *m_roleLabel;        // 角色标签（"You" / "Assistant"）
    QLabel *m_contentLabel;     // 消息文本标签
    QWidget *m_bubbleWidget;    // 气泡容器 widget（用于设置背景样式）
};
