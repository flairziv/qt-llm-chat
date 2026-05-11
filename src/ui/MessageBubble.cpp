#include "MessageBubble.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QMenu>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>

namespace {

QString attachmentLabelText(const Attachment &attachment)
{
    switch (attachment.type) {
    case Attachment::Image:
        return QStringLiteral("[Image] %1").arg(attachment.fileName);
    case Attachment::Document:
        return QStringLiteral("[Document] %1").arg(attachment.fileName);
    case Attachment::TextFile:
        return QStringLiteral("[Text] %1").arg(attachment.fileName);
    }

    return attachment.fileName;
}

} // namespace

MessageBubble::MessageBubble(Role role, const QString &content,
                             QWidget *parent,
                             const QString &userName,
                             const QString &assistantName,
                             int index,
                             bool favorite,
                             const QString &reasoning)
    : MessageBubble(role, content, {}, parent, userName, assistantName, index, favorite, reasoning)
{
}

MessageBubble::MessageBubble(Role role, const QString &content,
                             const QList<Attachment> &attachments,
                             QWidget *parent,
                             const QString &userName,
                             const QString &assistantName,
                             int index,
                             bool favorite,
                             const QString &reasoning)
    : QWidget(parent)
    , m_role(role)
    , m_content(content)
    , m_reasoning(reasoning)
    , m_userName(userName)
    , m_assistantName(assistantName)
    , m_attachments(attachments)
    , m_index(index)
    , m_favorite(favorite)
{
    setupUI();
}

void MessageBubble::setupUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 4, 0, 4);
    outerLayout->setSpacing(4);

    m_roleLabel = new QLabel(this);
    m_roleLabel->setObjectName(m_role == User ? "userRoleLabel" : "assistantRoleLabel");
    QFont roleFont = m_roleLabel->font();
    roleFont.setBold(true);
    roleFont.setPointSize(10);
    m_roleLabel->setFont(roleFont);
    refreshRoleLabel();

    m_bubbleWidget = new QWidget(this);
    m_bubbleWidget->setObjectName(m_role == User ? "userBubble" : "assistantBubble");
    m_bubbleWidget->setMaximumWidth(560);

    m_bubbleLayout = new QVBoxLayout(m_bubbleWidget);
    m_bubbleLayout->setContentsMargins(14, 10, 14, 10);
    m_bubbleLayout->setSpacing(8);

    m_contentLabel = new QLabel(m_content, m_bubbleWidget);
    m_contentLabel->setObjectName(m_role == User ? "userBubbleContent" : "assistantBubbleContent");
    m_contentLabel->setWordWrap(true);
    m_contentLabel->setTextFormat(Qt::PlainText);
    m_contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_contentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_contentLabel->setVisible(!m_content.isEmpty());

    // Reasoning 折叠区块仅 Assistant 角色构造；rebuildAttachmentWidgets 会把它放在 layout 顶部
    if (m_role == Assistant) {
        buildReasoningSection();
    }

    rebuildAttachmentWidgets();
    updateReasoningUi();

    if (m_role == User) {
        m_roleLabel->setAlignment(Qt::AlignRight);
        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addStretch();
        bubbleRow->addWidget(m_bubbleWidget);
        outerLayout->addWidget(m_roleLabel);
        outerLayout->addLayout(bubbleRow);
    } else {
        m_roleLabel->setAlignment(Qt::AlignLeft);
        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addWidget(m_bubbleWidget);
        bubbleRow->addStretch();
        outerLayout->addWidget(m_roleLabel);
        outerLayout->addLayout(bubbleRow);
    }

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &MessageBubble::onContextMenu);
}

void MessageBubble::rebuildAttachmentWidgets()
{
    while (m_bubbleLayout->count() > 0) {
        QLayoutItem *item = m_bubbleLayout->takeAt(0);
        QWidget *w = item->widget();
        // m_reasoningContainer 是稳定子 widget（仅 Assistant 角色一次性构造），
        // 不能 deleteLater——下一次 rebuild 还要复用。其他附件 widget 才是临时的。
        if (w && w != m_reasoningContainer) {
            w->deleteLater();
        }
        delete item;
    }

    // Reasoning 折叠区块永远放在最上方，让用户先看到 thinking 标题再看附件 / 正文
    if (m_reasoningContainer) {
        m_bubbleLayout->addWidget(m_reasoningContainer);
    }

    for (const Attachment &attachment : m_attachments) {
        if (QWidget *attachmentWidget = createAttachmentWidget(attachment)) {
            m_bubbleLayout->addWidget(attachmentWidget);
        }
    }

    m_bubbleLayout->addWidget(m_contentLabel);
}

QWidget *MessageBubble::createAttachmentWidget(const Attachment &attachment)
{
    if (attachment.type == Attachment::Image) {
        QLabel *imageLabel = new QLabel(m_bubbleWidget);
        QPixmap pixmap;
        pixmap.loadFromData(attachment.fileData);
        if (!pixmap.isNull()) {
            if (pixmap.width() > 400) {
                pixmap = pixmap.scaledToWidth(400, Qt::SmoothTransformation);
            }
            imageLabel->setPixmap(pixmap);
        } else {
            imageLabel->setText(attachmentLabelText(attachment));
        }
        imageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        imageLabel->setWordWrap(true);
        return imageLabel;
    }

    QLabel *label = new QLabel(attachmentLabelText(attachment), m_bubbleWidget);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(
        "background: rgba(128,128,128,0.08);"
        "border-radius: 6px;"
        "padding: 6px 10px;");
    return label;
}

void MessageBubble::appendText(const QString &text)
{
    m_content += text;
    m_contentLabel->setText(m_content);
    m_contentLabel->setVisible(!m_content.isEmpty());
    updateGeometry();
}

void MessageBubble::setContent(const QString &content)
{
    m_content = content;
    m_contentLabel->setText(m_content);
    m_contentLabel->setVisible(!m_content.isEmpty());
    updateGeometry();
}

void MessageBubble::setAttachments(const QList<Attachment> &attachments)
{
    m_attachments = attachments;
    rebuildAttachmentWidgets();
    m_contentLabel->setVisible(!m_content.isEmpty());
    updateGeometry();
}

QString MessageBubble::content() const
{
    return m_content;
}

MessageBubble::Role MessageBubble::role() const
{
    return m_role;
}

void MessageBubble::setRoleName(const QString &name)
{
    if (m_role == User) {
        m_userName = name;
    } else {
        m_assistantName = name;
    }
    refreshRoleLabel();
}

int MessageBubble::messageIndex() const
{
    return m_index;
}

void MessageBubble::setMessageIndex(int index)
{
    m_index = index;
}

bool MessageBubble::isFavorite() const
{
    return m_favorite;
}

void MessageBubble::setFavorite(bool favorite)
{
    if (m_favorite == favorite) {
        return;
    }
    m_favorite = favorite;
    m_bubbleWidget->setProperty("favorite", favorite);
    // 切换 dynamic property 后必须 unpolish/polish 才能让 QSS 的 [favorite="true"]
    // selector 重新匹配；否则视觉上不会立刻变化。
    m_bubbleWidget->style()->unpolish(m_bubbleWidget);
    m_bubbleWidget->style()->polish(m_bubbleWidget);
    refreshRoleLabel();
}

void MessageBubble::refreshRoleLabel()
{
    const QString name = (m_role == User) ? m_userName : m_assistantName;
    m_roleLabel->setText(m_favorite ? QStringLiteral("* ") + name : name);
}

void MessageBubble::onContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("Copy"));
    QAction *favAction = menu.addAction(m_favorite ? tr("Unfavorite") : tr("Favorite"));
    // assistant 气泡多一个"重新生成"项：截断到本条之前的 user 消息后重发
    QAction *regenAction = nullptr;
    if (m_role == Assistant) {
        regenAction = menu.addAction(tr("Regenerate from here"));
    }
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("Delete from here"));

    QAction *selected = menu.exec(mapToGlobal(pos));
    if (!selected) {
        return;
    }

    if (selected == copyAction) {
        QApplication::clipboard()->setText(m_content);
    } else if (selected == favAction) {
        emit favoriteToggleRequested(m_index);
    } else if (regenAction && selected == regenAction) {
        emit regenerateRequested(m_index);
    } else if (selected == deleteAction) {
        emit deleteFromHereRequested(m_index);
    }
}

// ============================================================================
// Reasoning（思考链）折叠区块
// ============================================================================

/**
 * @brief 构造 reasoning 容器（仅 Assistant 角色一次性调用）
 *
 * 容器布局：
 *   [💭 Thinking ▶/▼]   头部按钮：QToolButton autoRaise，点击切换 collapsed
 *   [reasoning body]    QLabel 显示文本，italic + 半透明背景，collapsed 时隐藏
 *
 * 容器永远存在于 m_bubbleLayout 顶部（rebuildAttachmentWidgets 每次重新插入），
 * 但若 m_reasoning 为空整体 hide()，看起来像没有这个区块。
 */
void MessageBubble::buildReasoningSection()
{
    m_reasoningContainer = new QWidget(m_bubbleWidget);
    m_reasoningContainer->setObjectName("reasoningContainer");
    QVBoxLayout *layout = new QVBoxLayout(m_reasoningContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_reasoningHeader = new QToolButton(m_reasoningContainer);
    m_reasoningHeader->setAutoRaise(true);
    m_reasoningHeader->setCursor(Qt::PointingHandCursor);
    m_reasoningHeader->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_reasoningHeader->setStyleSheet(
        QStringLiteral("QToolButton { padding: 2px 6px; color: rgba(128,128,128,0.85); "
                       "font-size: 12px; border: 0; }"
                       "QToolButton:hover { color: #10a37f; }"));
    connect(m_reasoningHeader, &QToolButton::clicked, this, [this]() {
        m_reasoningCollapsed = !m_reasoningCollapsed;
        updateReasoningUi();
    });

    m_reasoningBody = new QLabel(m_reasoningContainer);
    m_reasoningBody->setWordWrap(true);
    m_reasoningBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reasoningBody->setContextMenuPolicy(Qt::NoContextMenu);
    m_reasoningBody->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_reasoningBody->setStyleSheet(
        QStringLiteral("QLabel { color: rgba(128,128,128,0.95); font-style: italic; "
                       "font-size: 12px; padding: 6px 10px; "
                       "background: rgba(128,128,128,0.08); border-radius: 6px; }"));

    layout->addWidget(m_reasoningHeader, 0, Qt::AlignLeft);
    layout->addWidget(m_reasoningBody);
}

/**
 * @brief 根据 m_reasoning + m_reasoningCollapsed 刷新头部箭头、body 可见性、容器整体可见性
 */
void MessageBubble::updateReasoningUi()
{
    if (!m_reasoningContainer) return;
    const bool hasReasoning = !m_reasoning.isEmpty();
    m_reasoningContainer->setVisible(hasReasoning);
    if (!hasReasoning) return;

    // ▶ U+25B6 / ▼ U+25BC
    const QString arrow = m_reasoningCollapsed
        ? QStringLiteral("\xe2\x96\xb6")
        : QStringLiteral("\xe2\x96\xbc");
    // 💭 U+1F4AD
    m_reasoningHeader->setText(
        QStringLiteral("\xf0\x9f\x92\xad Thinking %1").arg(arrow));

    m_reasoningBody->setText(m_reasoning);
    m_reasoningBody->setVisible(!m_reasoningCollapsed);
}

void MessageBubble::setReasoning(const QString &reasoning)
{
    m_reasoning = reasoning;
    updateReasoningUi();
}

void MessageBubble::appendReasoning(const QString &token)
{
    m_reasoning += token;
    updateReasoningUi();
}
