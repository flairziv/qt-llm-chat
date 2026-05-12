#pragma once

#include <QDateTime>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ChatSession.h"

class QToolButton;
class ShimmerWidget;

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
                           const QDateTime &timestamp = QDateTime(),
                           const QString &reasoning = QString());

    explicit MessageBubble(Role role, const QString &content,
                           const QList<Attachment> &attachments,
                           QWidget *parent = nullptr,
                           const QString &userName = "You",
                           const QString &assistantName = "Assistant",
                           int index = -1,
                           bool favorite = false,
                           const QDateTime &timestamp = QDateTime(),
                           const QString &reasoning = QString());

    void appendText(const QString &text);
    void setContent(const QString &content);
    void setAttachments(const QList<Attachment> &attachments);
    void setRoleName(const QString &name);
    // 正文字体像素大小（默认 14）：联动图片宽度和占位符边长按比例缩放。
    // 角色标签字号 = pixelSize - 3，最小 9px。
    void setContentFontSize(int pixelSize);
    // 气泡最大宽度（ChatPage::resizeEvent 按视窗宽度 75% 持续推送）。
    void setMaxBubbleWidth(int width);
    // 主题切换后由 ChatPage 集中调用，重刷与 ElaTheme 相关的颜色（时间戳、文档附件背景）。
    // 不在 bubble 内部连接 eTheme：大量气泡创建/销毁会导致连接列表震荡。
    void rerenderForTheme();
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

    // ===== 图片生成占位符 =====
    /** @brief 在气泡里插入一个 ShimmerWidget 占位符（用于图片生成等待期） */
    void addImagePlaceholder();
    /** @brief 移除所有占位符（响应完成 / 出错 / 中止时调用） */
    void clearImagePlaceholders();
    /** @brief 是否含有图片占位符 */
    bool hasImagePlaceholder() const;

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
    void updateTimestampDisplay();
    static QString relativeTime(const QDateTime &dt);

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
    QDateTime m_timestamp;
    int m_index;
    bool m_favorite;

    QLabel *m_roleLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_contentLabel = nullptr;
    QWidget *m_bubbleWidget = nullptr;
    QVBoxLayout *m_bubbleLayout = nullptr;

    // Reasoning 折叠区块（仅 Assistant 角色非空时可见）
    QWidget *m_reasoningContainer = nullptr;
    QToolButton *m_reasoningHeader = nullptr;
    QLabel *m_reasoningBody = nullptr;
    bool m_reasoningCollapsed = true;   // 默认折叠：长 thinking 块不抢占视觉

    // 图片生成等待期的占位符。rebuildAttachmentWidgets 把 layout 清光时
    // 这些指针随之 deleteLater，列表必须紧跟着 clear() 避免悬空。
    QList<ShimmerWidget *> m_placeholders;

    // 正文字体像素大小（Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0 由 ChatPage 驱动）。
    // 基线 14px：图片宽度 / 占位符边长 / 角色标签字号都按此值线性缩放。
    int m_fontPixelSize = 14;
};
