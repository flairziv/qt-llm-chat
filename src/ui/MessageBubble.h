#pragma once

#include <QLabel>
#include <QList>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ChatSession.h"

class MessageBubble : public QWidget
{
    Q_OBJECT
public:
    enum Role { User, Assistant };

    explicit MessageBubble(Role role, const QString &content,
                           QWidget *parent = nullptr,
                           const QString &userName = "You",
                           const QString &assistantName = "Assistant",
                           int index = -1,
                           bool favorite = false);

    explicit MessageBubble(Role role, const QString &content,
                           const QList<Attachment> &attachments,
                           QWidget *parent = nullptr,
                           const QString &userName = "You",
                           const QString &assistantName = "Assistant",
                           int index = -1,
                           bool favorite = false);

    void appendText(const QString &text);
    void setContent(const QString &content);
    void setAttachments(const QList<Attachment> &attachments);
    void setRoleName(const QString &name);
    QString content() const;
    Role role() const;

    int messageIndex() const;
    void setMessageIndex(int index);
    bool isFavorite() const;
    void setFavorite(bool favorite);

signals:
    void favoriteToggleRequested(int index);
    void deleteFromHereRequested(int index);
    void regenerateRequested(int index);    // 重新生成该 assistant 回复（仅 assistant 气泡触发）

private slots:
    void onContextMenu(const QPoint &pos);

private:
    void setupUI();
    void rebuildAttachmentWidgets();
    void refreshRoleLabel();
    QWidget *createAttachmentWidget(const Attachment &attachment);

    Role m_role;
    QString m_content;
    QString m_userName;
    QString m_assistantName;
    QList<Attachment> m_attachments;
    int m_index;
    bool m_favorite;

    QLabel *m_roleLabel = nullptr;
    QLabel *m_contentLabel = nullptr;
    QWidget *m_bubbleWidget = nullptr;
    QVBoxLayout *m_bubbleLayout = nullptr;
};
