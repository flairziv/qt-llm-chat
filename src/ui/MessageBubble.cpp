#include "MessageBubble.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QApplication>
#include <QClipboard>

// ============================================================================
// 构造函数
// ============================================================================

MessageBubble::MessageBubble(Role role, const QString &content, QWidget *parent,
                             const QString &userName, const QString &assistantName,
                             int index, bool favorite)
    : QWidget(parent), m_role(role), m_content(content),
      m_userName(userName), m_assistantName(assistantName),
      m_index(index), m_favorite(favorite)
{
    setupUI();
}

// ============================================================================
// UI 构建
// ============================================================================

/**
 * @brief 根据角色构建消息气泡的布局
 *
 * 布局结构（以 User 为例，右对齐）：
 *
 *   outerLayout (VBox, 外边距 0/4/0/4)
 *   ├── m_roleLabel ("You", 右对齐, 粗体 10pt)
 *   └── bubbleRow (HBox)
 *       ├── <stretch>         ← 将气泡推到右侧
 *       └── m_bubbleWidget    ← 最大宽度 560px
 *           └── bubbleLayout (VBox, 内边距 14/10/14/10)
 *               └── m_contentLabel (自动换行, 纯文本, 可鼠标选中)
 *
 * Assistant 布局与 User 对称：stretch 在右侧，气泡左对齐。
 */
void MessageBubble::setupUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 4, 0, 4);
    outerLayout->setSpacing(4);

    // --- 角色标签 ---
    m_roleLabel = new QLabel;
    m_roleLabel->setObjectName(m_role == User ? "userRoleLabel" : "assistantRoleLabel");
    QFont roleFont = m_roleLabel->font();
    roleFont.setBold(true);
    roleFont.setPointSize(10);
    m_roleLabel->setFont(roleFont);
    refreshRoleLabel();  // 根据 m_favorite 决定是否前缀 "★ "

    // --- 气泡容器 ---
    m_bubbleWidget = new QWidget;
    m_bubbleWidget->setObjectName(m_role == User ? "userBubble" : "assistantBubble");
    m_bubbleWidget->setMaximumWidth(560);  // 限制气泡最大宽度，避免过长

    QVBoxLayout *bubbleLayout = new QVBoxLayout(m_bubbleWidget);
    bubbleLayout->setContentsMargins(14, 10, 14, 10);

    // --- 消息文本标签 ---
    m_contentLabel = new QLabel(m_content);
    m_contentLabel->setObjectName(m_role == User ? "userBubbleContent" : "assistantBubbleContent");
    m_contentLabel->setWordWrap(true);                              // 自动换行
    m_contentLabel->setTextFormat(Qt::PlainText);                   // 纯文本模式（防止 HTML 注入）
    m_contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);  // 允许鼠标选中复制
    m_contentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    bubbleLayout->addWidget(m_contentLabel);

    // --- 根据角色决定对齐方式 ---
    if (m_role == User) {
        // User: 角色标签右对齐，气泡靠右
        m_roleLabel->setAlignment(Qt::AlignRight);
        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addStretch();              // 左侧弹簧 → 气泡靠右
        bubbleRow->addWidget(m_bubbleWidget);
        outerLayout->addWidget(m_roleLabel);
        outerLayout->addLayout(bubbleRow);
    } else {
        // Assistant: 角色标签左对齐，气泡靠左
        m_roleLabel->setAlignment(Qt::AlignLeft);
        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addWidget(m_bubbleWidget);
        bubbleRow->addStretch();              // 右侧弹簧 → 气泡靠左
        outerLayout->addWidget(m_roleLabel);
        outerLayout->addLayout(bubbleRow);
    }

    // --- 右键自定义菜单 ---
    // 启用整张气泡 widget 的自定义右键菜单（含角色标签和气泡内容区）
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &MessageBubble::onContextMenu);
}

// ============================================================================
// 文本操作
// ============================================================================

/**
 * @brief 追加文本到气泡（用于流式 token 逐步显示）
 *
 * 每次追加后调用 updateGeometry() 通知布局系统重新计算尺寸，
 * 使气泡高度随文本增长自动扩展。
 */
void MessageBubble::appendText(const QString &text)
{
    m_content += text;
    m_contentLabel->setText(m_content);
    updateGeometry();
}

/** @brief 替换气泡的全部文本内容 */
void MessageBubble::setContent(const QString &content)
{
    m_content = content;
    m_contentLabel->setText(m_content);
    updateGeometry();
}

QString MessageBubble::content() const { return m_content; }
MessageBubble::Role MessageBubble::role() const { return m_role; }

void MessageBubble::setRoleName(const QString &name)
{
    if (m_role == User) m_userName = name;
    else m_assistantName = name;
    refreshRoleLabel();
}

// ============================================================================
// 索引与收藏状态
// ============================================================================

int MessageBubble::messageIndex() const { return m_index; }
void MessageBubble::setMessageIndex(int index) { m_index = index; }
bool MessageBubble::isFavorite() const { return m_favorite; }

/**
 * @brief 切换收藏状态并刷新角色标签
 *
 * 收藏时角色名前会拼上 "★ " 前缀，无需额外控件。
 */
void MessageBubble::setFavorite(bool favorite)
{
    if (m_favorite == favorite) return;
    m_favorite = favorite;
    refreshRoleLabel();
}

/**
 * @brief 根据当前角色与收藏状态重建角色标签文本
 *
 * 收藏  ：" ★ 名字"   （前缀星号）
 * 未收藏：" 名字"
 *
 * 由构造、setRoleName、setFavorite 共同调用，保持单一来源。
 */
void MessageBubble::refreshRoleLabel()
{
    QString name = (m_role == User) ? m_userName : m_assistantName;
    m_roleLabel->setText(m_favorite ? QStringLiteral("★ ") + name : name);
}

// ============================================================================
// 右键菜单
// ============================================================================

/**
 * @brief 在鼠标右键位置弹出自定义菜单
 *
 * 当前菜单包含：
 *   - Copy：将气泡文本内容复制到系统剪贴板
 *   - Unfavorite / Favorite：切换收藏状态（发射 favoriteToggleRequested 信号）
 *   - Delete from here：删除该消息及其后全部消息（发射 deleteFromHereRequested 信号）
 *
 * 后续将追加 Edit 等操作。
 *
 * 注意：m_contentLabel 设置了 TextSelectableByMouse，自带文本选中行为，
 * 不会拦截右键事件 —— 整个 MessageBubble widget 的 customContextMenuRequested
 * 信号仍能正常触发本槽函数。
 *
 * 收藏切换不在本控件内直接落库：仅发出信号，由上层（ChatPage → MainWindow）
 * 调用 ChatSession::setMessageFavorite() 后回头调本控件 setFavorite() 刷新 UI。
 */
void MessageBubble::onContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("Copy"));
    QAction *favAction = menu.addAction(m_favorite ? tr("Unfavorite") : tr("Favorite"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("Delete from here"));

    QAction *selected = menu.exec(mapToGlobal(pos));  // 阻塞式弹出菜单
    if (!selected) return;

    if (selected == copyAction) {
        QApplication::clipboard()->setText(m_content);
    } else if (selected == favAction) {
        emit favoriteToggleRequested(m_index);
    } else if (selected == deleteAction) {
        emit deleteFromHereRequested(m_index);
    }
}
