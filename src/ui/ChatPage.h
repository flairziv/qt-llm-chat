#pragma once

#include <QHBoxLayout>
#include <QLabel>
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
    void clearMessages();
    void loadMessages(const QList<ChatMessage> &messages);
    void setBubbleFavorite(int index, bool favorite);

    void setInputEnabled(bool enabled);
    void setStatusText(const QString &text);
    void scrollToBottom();

    void addSession(class ChatSession *session);
    void removeSession(const QString &id);
    void setActiveSession(const QString &id);
    void refreshSessionList(const QList<ChatSession *> &sessions);

    void setProviderIndex(int index);
    QString currentProviderData() const;

    void updateRoleNames(const QString &userName, const QString &assistantName);
    void refreshPromptTemplates();

signals:
    void sendMessageRequested(const QString &text, const QList<Attachment> &attachments);
    void sessionSelected(const QString &id);
    void sessionDeleteRequested(const QString &id);
    void sessionRenameRequested(const QString &id);
    void sessionExportRequested(const QString &id);
    void newChatRequested();
    void providerSwitched(const QString &providerName);
    void messageFavoriteToggleRequested(int index);
    void messageDeleteFromHereRequested(int index);

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
    QLabel *m_statusLabel = nullptr;
    QWidget *m_attachPreviewWidget = nullptr;
    QHBoxLayout *m_attachPreviewLayout = nullptr;

    QList<MessageBubble *> m_bubbles;
    QList<Attachment> m_pendingAttachments;

    AppSettings *m_settings = nullptr;
    QHBoxLayout *m_promptLayout = nullptr;

    QString m_userName = "You";
    QString m_assistantName = "Assistant";

    // 拖拽视觉反馈：dragEnter 期间在 ChatPage 边缘画一圈虚线，drop / leave 后清除
    bool m_dragHighlight = false;
};
