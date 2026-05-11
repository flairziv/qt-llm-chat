#pragma once

#include <QHash>
#include <QLabel>
#include <QList>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ChatSession.h"

class QToolButton;

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
                           bool favorite = false,
                           const QString &reasoning = QString());

    explicit MessageBubble(Role role, const QString &content,
                           const QList<Attachment> &attachments,
                           QWidget *parent = nullptr,
                           const QString &userName = "You",
                           const QString &assistantName = "Assistant",
                           int index = -1,
                           bool favorite = false,
                           const QString &reasoning = QString());

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

    // ===== Reasoning（思考链）折叠区块 =====
    /** @brief 一次性把 reasoning 全文写入气泡（loadMessages 路径用） */
    void setReasoning(const QString &reasoning);
    /** @brief 流式 token 增量追加 reasoning（onReasoningTokenReceived 路径用） */
    void appendReasoning(const QString &token);
    /** @brief 当前 reasoning 文本（用于持久化 / 测试） */
    QString reasoning() const { return m_reasoning; }

signals:
    void favoriteToggleRequested(int index);
    void deleteFromHereRequested(int index);
    void regenerateRequested(int index);    // 重新生成该 assistant 回复（仅 assistant 气泡触发）

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onContextMenu(const QPoint &pos);

private:
    void setupUI();
    void rebuildAttachmentWidgets();
    void refreshRoleLabel();
    QWidget *createAttachmentWidget(const Attachment &attachment, int idx);
    void buildReasoningSection();   // setupUI 时构造（assistant only）
    void updateReasoningUi();       // 根据 m_reasoning + collapse 状态刷新 header / body / 可见性

    Role m_role;
    QString m_content;
    QString m_reasoning;
    QString m_userName;
    QString m_assistantName;
    QList<Attachment> m_attachments;
    // 图片附件解码后的原始 QPixmap 按附件索引缓存：
    //   - 避免 rebuildAttachmentWidgets 反复 loadFromData 触发的 PNG/JPEG 解码
    //   - eventFilter 弹大图也从这里直接取，省去二次解码
    // 仅 Image 类型有缓存项；setAttachments 清空。
    QHash<int, QPixmap> m_originalPixmapCache;
    int m_index;
    bool m_favorite;

    QLabel *m_roleLabel = nullptr;
    QLabel *m_contentLabel = nullptr;
    QWidget *m_bubbleWidget = nullptr;
    QVBoxLayout *m_bubbleLayout = nullptr;

    // Reasoning 折叠区块（仅 Assistant 角色非空时可见）
    QWidget *m_reasoningContainer = nullptr;
    QToolButton *m_reasoningHeader = nullptr;
    QLabel *m_reasoningBody = nullptr;
    bool m_reasoningCollapsed = true;   // 默认折叠：长 thinking 块不抢占视觉
};
