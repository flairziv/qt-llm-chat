#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLabel>
#include <QList>

class MessageBubble;
class SessionListWidget;
class ElaComboBox;
class ElaPushButton;
class AppSettings;
struct ChatMessage;

/**
 * @brief 聊天主页面 —— 整个应用的核心交互界面
 *
 * 采用左右分栏布局（QSplitter）：
 *
 *   ┌─────────────────────────────────────────────────────┐
 *   │ ┌──────────┐ ┌──────────────────────────────────┐   │
 *   │ │LeftPanel │ │         Right Panel               │   │
 *   │ │          │ │                                    │   │
 *   │ │ ComboBox │ │   QScrollArea（消息气泡区）        │   │
 *   │ │ +NewChat │ │     MessageBubble ...              │   │
 *   │ │          │ │                                    │   │
 *   │ │ Session  │ │                                    │   │
 *   │ │ List     │ │──────────────────────────────────│   │
 *   │ │ Widget   │ │   StatusLabel                     │   │
 *   │ │          │ │──────────────────────────────────│   │
 *   │ │          │ │   [ TextEdit 输入框 ] [Send]      │   │
 *   │ └──────────┘ └──────────────────────────────────┘   │
 *   └─────────────────────────────────────────────────────┘
 *
 * 本类只负责 UI 展示和用户交互，不直接处理业务逻辑。
 * 所有操作通过信号通知 MainWindow 进行调度。
 */
class ChatPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChatPage(AppSettings *settings, QWidget *parent = nullptr);

    // ===== 消息管理 =====
    void addMessageBubble(const QString &role, const QString &content);  // 添加一个消息气泡
    void appendToLastBubble(const QString &token);  // 向最后一个气泡追加文本（流式填充）
    void replaceLastBubbleContent(const QString &text);  // 替换最后一个气泡的完整文本
    void clearMessages();                            // 清空所有消息气泡
    void loadMessages(const QList<ChatMessage> &messages);  // 批量加载消息记录

    // ===== UI 状态控制 =====
    void setInputEnabled(bool enabled);  // 启用/禁用输入框和发送按钮
    void setStatusText(const QString &text);  // 设置状态提示文本（如 "Thinking..."）
    void scrollToBottom();               // 滚动消息区到底部

    // ===== 会话列表代理方法 =====
    void addSession(class ChatSession *session);
    void removeSession(const QString &id);
    void setActiveSession(const QString &id);
    void refreshSessionList(const QList<ChatSession*> &sessions);

    // ===== Provider 选择 =====
    void setProviderIndex(int index);          // 设置 Provider 下拉框选中项
    QString currentProviderData() const;       // 获取当前选中的 Provider 标识

    // ===== 角色显示名 =====
    void updateRoleNames(const QString &userName, const QString &assistantName);  // 更新所有气泡的角色标签

    // ===== Prompt 模板 =====
    void refreshPromptTemplates();  // 重新加载 prompt 模板栏（从 AppSettings 读取）

signals:
    void sendMessageRequested(const QString &text);      // 用户请求发送消息
    void sessionSelected(const QString &id);             // 用户点击选中某个会话
    void sessionDeleteRequested(const QString &id);      // 用户请求删除某个会话
    void newChatRequested();                              // 用户点击"新建聊天"
    void providerSwitched(const QString &providerName);  // 用户切换 Provider

private slots:
    void onSendClicked();  // 发送按钮点击处理

private:
    void setupUI();        // 构建整个页面的 UI 布局
    bool eventFilter(QObject *obj, QEvent *event) override;  // 事件过滤器（拦截 Enter 键）

    // --- 左侧面板控件 ---
    ElaComboBox *m_providerCombo;      // Provider 选择下拉框（Claude / OpenAI）
    ElaPushButton *m_newChatBtn;       // "新建聊天"按钮
    SessionListWidget *m_sessionList;  // 会话列表控件

    // --- 右侧面板控件 ---
    QScrollArea *m_scrollArea;         // 消息区的滚动容器
    QWidget *m_messageContainer;       // 消息气泡的实际容器 widget
    QVBoxLayout *m_messageLayout;      // 消息气泡的垂直布局（末尾有 stretch）
    QTextEdit *m_inputEdit;            // 多行文本输入框
    ElaPushButton *m_sendButton;       // 发送按钮
    QLabel *m_statusLabel;             // 状态提示标签（"Thinking..." / "Error: ..."）

    QList<MessageBubble*> m_bubbles;   // 当前显示的所有消息气泡指针列表

    // AppSettings 引用（用于读取 prompt 模板、监听变化）
    AppSettings *m_settings = nullptr;
    QHBoxLayout *m_promptLayout = nullptr;

    // 角色显示名（默认 "You"/"Assistant"，可通过 updateRoleNames 更新）
    QString m_userName = "You";
    QString m_assistantName = "Assistant";
};
