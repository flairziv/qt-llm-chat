#include "MessageBubble.h"
#include "ImageViewerDialog.h"
#include "ShimmerWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QStandardPaths>
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
                             const QDateTime &timestamp,
                             const QString &reasoning)
    : MessageBubble(role, content, {}, parent, userName, assistantName,
                    index, favorite, timestamp, reasoning)
{
}

MessageBubble::MessageBubble(Role role, const QString &content,
                             const QList<Attachment> &attachments,
                             QWidget *parent,
                             const QString &userName,
                             const QString &assistantName,
                             int index,
                             bool favorite,
                             const QDateTime &timestamp,
                             const QString &reasoning)
    : QWidget(parent)
    , m_role(role)
    , m_content(content)
    , m_reasoning(reasoning)
    , m_userName(userName)
    , m_assistantName(assistantName)
    , m_attachments(attachments)
    , m_timestamp(timestamp.isValid() ? timestamp : QDateTime::currentDateTime())
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

    m_timeLabel = new QLabel(this);
    m_timeLabel->setObjectName("timeLabel");
    QFont timeFont = m_timeLabel->font();
    timeFont.setPointSize(9);
    m_timeLabel->setFont(timeFont);
    // 半透明灰色：dark / light 主题下都能看见，省去 ElaTheme 监听 + 主题切换重绘
    m_timeLabel->setStyleSheet(QStringLiteral("color: rgba(128,128,128,0.85);"));
    updateTimestampDisplay();

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

    // 角色行：QHBoxLayout 把 m_roleLabel + m_timeLabel 排到一起，stretch 控制对齐
    QHBoxLayout *roleRow = new QHBoxLayout;
    roleRow->setContentsMargins(0, 0, 0, 0);
    roleRow->setSpacing(6);

    if (m_role == User) {
        // User: ...stretch | time | role
        roleRow->addStretch();
        roleRow->addWidget(m_timeLabel);
        roleRow->addWidget(m_roleLabel);

        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addStretch();
        bubbleRow->addWidget(m_bubbleWidget);
        outerLayout->addLayout(roleRow);
        outerLayout->addLayout(bubbleRow);
    } else {
        // Assistant: role | time | stretch...
        roleRow->addWidget(m_roleLabel);
        roleRow->addWidget(m_timeLabel);
        roleRow->addStretch();

        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addWidget(m_bubbleWidget);
        bubbleRow->addStretch();
        outerLayout->addLayout(roleRow);
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

    // 占位符随 layout 一起 deleteLater，主动 clear 指针列表避免悬空
    m_placeholders.clear();

    // Reasoning 折叠区块永远放在最上方，让用户先看到 thinking 标题再看附件 / 正文
    if (m_reasoningContainer) {
        m_bubbleLayout->addWidget(m_reasoningContainer);
    }

    for (int idx = 0; idx < m_attachments.size(); ++idx) {
        if (QWidget *attachmentWidget = createAttachmentWidget(m_attachments.at(idx), idx)) {
            m_bubbleLayout->addWidget(attachmentWidget);
        }
    }

    m_bubbleLayout->addWidget(m_contentLabel);
}

QWidget *MessageBubble::createAttachmentWidget(const Attachment &attachment, int idx)
{
    if (attachment.type == Attachment::Image) {
        QLabel *imageLabel = new QLabel(m_bubbleWidget);

        // 命中缓存跳过解码；rebuildAttachmentWidgets 反复跑（reasoning / 字体调整）
        // 时不再每次都重走一遍 PNG/JPEG decoder
        QPixmap pixmap = m_originalPixmapCache.value(idx);
        if (pixmap.isNull()) {
            pixmap.loadFromData(attachment.fileData);
            if (!pixmap.isNull()) {
                m_originalPixmapCache.insert(idx, pixmap);
            }
        }

        if (!pixmap.isNull()) {
            QPixmap display = pixmap;
            if (display.width() > 400) {
                display = display.scaledToWidth(400, Qt::SmoothTransformation);
            }
            imageLabel->setPixmap(display);
        } else {
            imageLabel->setText(attachmentLabelText(attachment));
        }
        imageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        imageLabel->setWordWrap(true);

        // 点击弹大图：保存附件索引到属性，eventFilter 拦 LeftButton 释放后
        // 用 m_originalPixmapCache[idx] 喂 ImageViewerDialog
        imageLabel->setProperty("imgAttIdx", idx);
        imageLabel->setCursor(Qt::PointingHandCursor);
        imageLabel->setToolTip(tr("Click to view full size"));
        imageLabel->installEventFilter(this);
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
    // 附件被替换：旧索引指向的 pixmap 不再对应当前列表，整体丢弃
    m_originalPixmapCache.clear();
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

    // 图片附件：单图直接 "Save Image As..."；多图弹子菜单，每条对应一张图
    QList<int> imageIndices;
    for (int i = 0; i < m_attachments.size(); ++i) {
        if (m_attachments.at(i).type == Attachment::Image) imageIndices.append(i);
    }
    QAction *saveSingleAction = nullptr;
    QHash<QAction *, int> saveSubActions;
    if (!imageIndices.isEmpty()) {
        menu.addSeparator();
        if (imageIndices.size() == 1) {
            saveSingleAction = menu.addAction(tr("Save Image As..."));
        } else {
            QMenu *saveMenu = menu.addMenu(tr("Save Image As..."));
            for (int idx : imageIndices) {
                const Attachment &att = m_attachments.at(idx);
                const QString label = att.fileName.isEmpty()
                    ? tr("Image %1").arg(idx + 1)
                    : att.fileName;
                saveSubActions.insert(saveMenu->addAction(label), idx);
            }
        }
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
    } else if ((saveSingleAction && selected == saveSingleAction)
               || saveSubActions.contains(selected)) {
        // 直接落盘 att.fileData：附件存的就是原始编码字节（PNG/JPEG），
        // 不走 QImage 解码 + 重编码，避免无谓的画质损失和耗时
        const int targetIdx = saveSubActions.contains(selected)
            ? saveSubActions.value(selected)
            : imageIndices.first();
        if (targetIdx >= 0 && targetIdx < m_attachments.size()) {
            const Attachment &att = m_attachments.at(targetIdx);
            const QString defaultName = att.fileName.isEmpty()
                ? QStringLiteral("image.png")
                : att.fileName;
            const QString defaultDir = QStandardPaths::writableLocation(
                QStandardPaths::PicturesLocation);
            const QString suffix = QFileInfo(defaultName).suffix().toLower();
            // 过滤器顺序匹配实际文件后缀；用户拨过来的图大多是 PNG，所以默认放第一
            QString filter;
            if (suffix == "jpg" || suffix == "jpeg") {
                filter = tr("JPEG Image (*.jpg *.jpeg);;PNG Image (*.png);;All Files (*)");
            } else {
                filter = tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)");
            }
            const QString path = QFileDialog::getSaveFileName(this, tr("Save Image As"),
                defaultDir + "/" + defaultName, filter);
            if (path.isEmpty()) return;
            QFile out(path);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(att.fileData);
            } else {
                QMessageBox::warning(this, tr("Save Image"),
                    tr("Failed to write to:\n%1").arg(path));
            }
        }
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

/**
 * @brief 拦截图片 QLabel 的左键点击 → 弹 ImageViewerDialog 看大图
 *
 * createAttachmentWidget 在每个图片 QLabel 上设了 "imgAttIdx" 属性
 * 并 installEventFilter(this)。这里只处理 LeftButton release（避免和右键
 * contextMenuEvent 冲突，也跳过中键 / 拖拽 / 双击之类）。
 *
 * 原图直接从 m_originalPixmapCache 取，避免重复 PNG/JPEG 解码；cache miss
 * 时现解一次并塞回 cache，跟 createAttachmentWidget 走同一份缓存。
 */
bool MessageBubble::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton) {
            return QWidget::eventFilter(obj, event);
        }
        auto *label = qobject_cast<QLabel *>(obj);
        if (!label) {
            return QWidget::eventFilter(obj, event);
        }
        const QVariant prop = label->property("imgAttIdx");
        if (!prop.isValid()) {
            return QWidget::eventFilter(obj, event);
        }
        const int idx = prop.toInt();
        if (idx < 0 || idx >= m_attachments.size()) {
            return QWidget::eventFilter(obj, event);
        }
        const Attachment &att = m_attachments.at(idx);
        QPixmap orig = m_originalPixmapCache.value(idx);
        if (orig.isNull()) {
            orig.loadFromData(att.fileData);
            if (!orig.isNull()) {
                m_originalPixmapCache.insert(idx, orig);
            }
        }
        if (!orig.isNull()) {
            // ImageViewerDialog 构造里设了 WA_DeleteOnClose，show 后自负盈亏
            auto *viewer = new ImageViewerDialog(orig, this);
            viewer->show();
        }
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================================
// 时间戳显示
// ============================================================================

/** @brief 根据 m_timestamp 更新 m_timeLabel 文本 + tooltip */
void MessageBubble::updateTimestampDisplay()
{
    if (!m_timestamp.isValid()) {
        m_timeLabel->hide();
        return;
    }
    m_timeLabel->setText(relativeTime(m_timestamp));
    // hover 显示绝对时间方便核对：相对时间易看错（"5h ago" 是今天还是昨天）
    m_timeLabel->setToolTip(m_timestamp.toString("yyyy-MM-dd hh:mm:ss"));
}

/** @brief 把绝对时间转为 "just now" / "5m ago" / "2h ago" / "yesterday HH:mm" / "MM-dd HH:mm" / "yyyy-MM-dd" */
QString MessageBubble::relativeTime(const QDateTime &dt)
{
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = dt.secsTo(now);
    if (secs < 60) return QStringLiteral("just now");
    if (secs < 3600) return QStringLiteral("%1m ago").arg(secs / 60);
    if (secs < 86400) return QStringLiteral("%1h ago").arg(secs / 3600);
    if (secs < 172800) return QStringLiteral("yesterday ") + dt.toString("HH:mm");
    if (dt.date().year() == now.date().year()) return dt.toString("MM-dd HH:mm");
    return dt.toString("yyyy-MM-dd");
}

// ============================================================================
// 图片生成占位符（ShimmerWidget）
// ============================================================================
//
// - 触发：Provider 抛 imageGenerationStarted(count) →
//         MainWindow::onImageGenerationStarted → ChatPage::addImagePlaceholderToLastBubble
// - 插入位置：永远在 m_contentLabel 之前（reasoning / 已有 attachment 之后）
//   同一轮生成多张图时占位符顺次堆叠
// - 必须在 responseFinished / error / abort 任一路径手动 clear，否则会残留。
//   setAttachments 走 rebuildAttachmentWidgets 也会顺便清掉，是安全网。

void MessageBubble::addImagePlaceholder()
{
    auto *placeholder = new ShimmerWidget(m_bubbleWidget);
    placeholder->setObjectName("imagePlaceholder");
    placeholder->setFixedSize(200, 200);

    int insertPos = m_bubbleLayout->indexOf(m_contentLabel);
    if (insertPos < 0) insertPos = m_bubbleLayout->count();
    m_bubbleLayout->insertWidget(insertPos, placeholder);
    m_placeholders.append(placeholder);
}

void MessageBubble::clearImagePlaceholders()
{
    for (ShimmerWidget *p : m_placeholders) {
        if (p) {
            m_bubbleLayout->removeWidget(p);
            p->hide();
            p->deleteLater();
        }
    }
    m_placeholders.clear();
}

bool MessageBubble::hasImagePlaceholder() const
{
    return !m_placeholders.isEmpty();
}
