#pragma once

#include <QDateTime>
#include <QHBoxLayout>
#include <QList>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ChatSession.h"

class MessageBubble;
class SessionListWidget;
class ElaComboBox;
class ElaPushButton;
class AppSettings;
class AssistantLoadingWidget;
class QLineEdit;

class ChatPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChatPage(AppSettings *settings, QWidget *parent = nullptr);

    void addMessageBubble(const QString &role, const QString &content,
                          const QList<Attachment> &attachments = {},
                          int index = -1, bool favorite = false,
                          const QDateTime &timestamp = QDateTime(),
                          const QString &reasoning = QString());
    void appendToLastBubble(const QString &token);
    void appendReasoningToLastBubble(const QString &token);   // reasoning 流式增量写入
    void replaceLastBubbleContent(const QString &text);
    // 整段替换最后一个气泡的正文 + 附件（regenerate 路径 / 图像生成 markers 转 Attachment 后用）
    void replaceLastBubbleMessage(const QString &text, const QList<Attachment> &attachments);
    // 整段替换最后一个气泡的 reasoning 文本（loadMessages / 重试路径）
    void replaceLastBubbleReasoning(const QString &reasoning);
    // 把本轮工具调用 + 结果挂到最后一个气泡（assistant tool_use 气泡）的工具区块。
    // 合成的 tool_result 消息本身不单独成气泡，结果合并到这里展示。
    void addToolDataToLastBubble(const QList<ToolCall> &calls, const QList<ToolResult> &results);
    // 图片生成占位符（ShimmerWidget）：响应完成 / 出错任一路径都必须 clear，
    // MainWindow::onResponseFinished / onProviderError / abortStreamingAndSavePartial 已覆盖
    void addImagePlaceholderToLastBubble(int count = 1);
    void clearImagePlaceholdersInLastBubble();
    void removeLastBubble();                  // 删除最末一条气泡（用于 abort 后清理空 assistant 气泡）
    void clearMessages();
    void loadMessages(const QList<ChatMessage> &messages);
    void setBubbleFavorite(int index, bool favorite);

    void setInputEnabled(bool enabled);
    void setStatusText(const QString &text);
    void setLoading(bool loading);            // 控制 AssistantLoadingWidget 的眨眼动画
    void fillInputText(const QString &text);  // 把指定文本回填到输入框并聚焦（编辑消息用）
    void scrollToBottom();
    // 强制滚到底部并重启自动跟随。仅用于"用户主动想看到最新"的场景：
    // 发送消息、切换会话、加载历史。不要在流式 token 路径里调用——那里要尊重
    // m_autoScrollEnabled，让滚上去看历史的用户不被打断。
    void scrollToBottomForce();

    void addSession(class ChatSession *session);
    void removeSession(const QString &id);
    void setActiveSession(const QString &id);
    void moveSessionToTop(const QString &id);     // 把指定会话推到列表顶部（消息发完用）
    void refreshSessionList(const QList<ChatSession *> &sessions);

    void setProviderIndex(int index);
    QString currentProviderData() const;

    void updateRoleNames(const QString &userName, const QString &assistantName);
    void refreshPromptTemplates();

    // 消息搜索（Ctrl+F 触发）：对 m_bubbles 做大小写不敏感子串匹配，
    // 命中的气泡加金色虚线边框，第一个命中滚到可见区。空 keyword 即清除高亮。
    void searchInMessages(const QString &keyword);
    void clearSearchHighlight();

signals:
    void sendMessageRequested(const QString &text, const QList<Attachment> &attachments);
    void sessionSelected(const QString &id);
    void sessionDeleteRequested(const QString &id);
    void sessionRenameRequested(const QString &id, const QString &newName);
    void sessionExportRequested(const QString &id);
    void newChatRequested();
    void providerSwitched(const QString &providerName);
    void messageFavoriteToggleRequested(int index);
    void messageDeleteFromHereRequested(int index);
    void messageRegenerateRequested(int index);
    void messageEditRequested(int index);   // 用户右键 user 气泡 "Edit Message"

private slots:
    void onSendClicked();
    void onAttachClicked();
    // Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0 触发，按步进 2px 调整正文字号
    void zoomIn();
    void zoomOut();
    void zoomReset();

private:
    void setupUI();
    void applyFontSizeToAllBubbles();
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void updateBubbleMaxWidth();    // 按 m_scrollArea 视窗宽度推 75% 到所有气泡
    void addAttachmentFromFile(const QString &filePath);
    void addAttachmentFromImage(const QImage &image);
    void refreshAttachmentPreview();
    void clearAttachments();
    static bool isImageFile(const QString &suffix);
    static bool isDocumentFile(const QString &suffix);
    static QString mimeTypeForSuffix(const QString &suffix);

    ElaComboBox *m_providerCombo = nullptr;
    ElaPushButton *m_newChatBtn = nullptr;
    SessionListWidget *m_sessionList = nullptr;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_messageContainer = nullptr;
    QVBoxLayout *m_messageLayout = nullptr;
    QTextEdit *m_inputEdit = nullptr;
    ElaPushButton *m_attachButton = nullptr;
    ElaPushButton *m_sendButton = nullptr;
    AssistantLoadingWidget *m_statusWidget = nullptr;   // 流式期间的眨眼/扫视加载头像 + 状态文字
    QWidget *m_attachPreviewWidget = nullptr;
    QHBoxLayout *m_attachPreviewLayout = nullptr;

    // Ctrl+F 触发的消息搜索栏（默认隐藏，关闭按钮或清空输入恢复）
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QList<MessageBubble *> m_bubbles;
    QList<Attachment> m_pendingAttachments;

    // 流式期间是否自动跟随最新消息滚到底部。用户手动向上滚浏览历史时
    // 自动关闭；滚回底部时自动恢复。避免长回复持续把视野拉走。
    bool m_autoScrollEnabled = true;

    AppSettings *m_settings = nullptr;
    QHBoxLayout *m_promptLayout = nullptr;

    QString m_userName = "You";
    QString m_assistantName = "Assistant";

    // 拖拽视觉反馈：dragEnter 期间在 ChatPage 边缘画一圈虚线，drop / leave 后清除
    bool m_dragHighlight = false;

    // 正文字号（Ctrl+wheel / Ctrl+= / Ctrl+- 调整，Ctrl+0 恢复默认）。
    // 新建气泡 / loadMessages 会把当前值同步给每个 MessageBubble。
    int m_fontSize = 14;
    static const int kMinFontSize = 10;
    static const int kMaxFontSize = 28;
    static const int kDefaultFontSize = 14;
};
