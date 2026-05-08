#pragma once

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

class ChatPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChatPage(AppSettings *settings, QWidget *parent = nullptr);

    void addMessageBubble(const QString &role, const QString &content,
                          const QList<Attachment> &attachments = {},
                          int index = -1, bool favorite = false);
    void appendToLastBubble(const QString &token);
    void replaceLastBubbleContent(const QString &text);
    void removeLastBubble();                  // 删除最末一条气泡（用于 abort 后清理空 assistant 气泡）
    void clearMessages();
    void loadMessages(const QList<ChatMessage> &messages);
    void setBubbleFavorite(int index, bool favorite);

    void setInputEnabled(bool enabled);
    void setStatusText(const QString &text);
    void setLoading(bool loading);            // 控制 AssistantLoadingWidget 的眨眼动画
    void scrollToBottom();

    void addSession(class ChatSession *session);
    void removeSession(const QString &id);
    void setActiveSession(const QString &id);
    void moveSessionToTop(const QString &id);     // 把指定会话推到列表顶部（消息发完用）
    void refreshSessionList(const QList<ChatSession *> &sessions);

    void setProviderIndex(int index);
    QString currentProviderData() const;

    void updateRoleNames(const QString &userName, const QString &assistantName);
    void refreshPromptTemplates();

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

private slots:
    void onSendClicked();
    void onAttachClicked();

private:
    void setupUI();
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
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
};
