#include "ChatPage.h"

#include "MessageBubble.h"
#include "SessionListWidget.h"
#include "AssistantLoadingWidget.h"
#include "core/AppSettings.h"

#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <QBuffer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QSplitter>
#include <QStringList>
#include <QTextCursor>
#include <QTimer>

ChatPage::ChatPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 启用拖拽：dragEnter / dragLeave / drop / paintEvent 由本类统一处理。
    // m_inputEdit 自己也接收拖拽事件（QTextEdit 默认行为），创建时会显式关掉。
    setAcceptDrops(true);
    setupUI();
    refreshPromptTemplates();

    if (m_settings) {
        connect(m_settings, &AppSettings::promptTemplatesChanged,
                this, &ChatPage::refreshPromptTemplates);
    }
}

void ChatPage::setupUI()
{
    QHBoxLayout *pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    QWidget *leftPanel = new QWidget(splitter);
    leftPanel->setObjectName("leftPanel");
    leftPanel->setFixedWidth(260);

    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(8);

    QHBoxLayout *headerLayout = new QHBoxLayout;
    m_providerCombo = new ElaComboBox(leftPanel);
    m_providerCombo->setObjectName("providerCombo");
    m_providerCombo->addItem("Claude", "claude");
    m_providerCombo->addItem("OpenAI", "openai");
    m_providerCombo->addItem("Gemini", "gemini");

    m_newChatBtn = new ElaPushButton("+ New", leftPanel);
    m_newChatBtn->setObjectName("newChatButton");

    headerLayout->addWidget(m_providerCombo, 1);
    headerLayout->addWidget(m_newChatBtn);
    leftLayout->addLayout(headerLayout);

    m_sessionList = new SessionListWidget(leftPanel);
    leftLayout->addWidget(m_sessionList, 1);

    QWidget *rightPanel = new QWidget(splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_scrollArea = new QScrollArea(rightPanel);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setObjectName("chatScrollArea");
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_messageContainer = new QWidget(m_scrollArea);
    m_messageContainer->setObjectName("messageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setSpacing(12);
    m_messageLayout->setContentsMargins(20, 20, 20, 20);
    m_messageLayout->addStretch(1);

    m_scrollArea->setWidget(m_messageContainer);
    rightLayout->addWidget(m_scrollArea, 1);

    // 旧的纯文字 m_statusLabel 已被 AssistantLoadingWidget 取代（保留指针为 nullptr 以
    // 兼容历史 setStatusText 调用，但所有显示都走 m_statusWidget）
    m_statusWidget = new AssistantLoadingWidget(rightPanel);
    m_statusWidget->setObjectName("assistantStatus");
    m_statusWidget->hide();
    rightLayout->addWidget(m_statusWidget);

    m_attachPreviewWidget = new QWidget(rightPanel);
    m_attachPreviewWidget->setObjectName("attachPreviewWidget");
    m_attachPreviewWidget->hide();
    m_attachPreviewLayout = new QHBoxLayout(m_attachPreviewWidget);
    m_attachPreviewLayout->setContentsMargins(16, 6, 16, 0);
    m_attachPreviewLayout->setSpacing(8);
    m_attachPreviewLayout->addStretch();
    rightLayout->addWidget(m_attachPreviewWidget, 0);

    QWidget *promptBar = new QWidget(rightPanel);
    promptBar->setObjectName("promptBar");
    m_promptLayout = new QHBoxLayout(promptBar);
    m_promptLayout->setContentsMargins(16, 4, 16, 0);
    m_promptLayout->setSpacing(6);
    m_promptLayout->addStretch();
    rightLayout->addWidget(promptBar, 0);

    QWidget *inputBar = new QWidget(rightPanel);
    inputBar->setObjectName("inputBar");
    inputBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout *inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(16, 10, 16, 10);

    m_attachButton = new ElaPushButton("+", inputBar);
    m_attachButton->setObjectName("attachButton");
    m_attachButton->setFixedSize(44, 44);
    m_attachButton->setToolTip("Attach image or file");

    m_inputEdit = new QTextEdit(inputBar);
    m_inputEdit->setObjectName("chatInput");
    m_inputEdit->setPlaceholderText("Type a message... (Enter to send, Shift+Enter for newline)");
    m_inputEdit->setMaximumHeight(120);
    m_inputEdit->setFixedHeight(44);
    m_inputEdit->setAcceptDrops(false); // 拖拽统一由 ChatPage 处理，避免事件被 QTextEdit 吞掉
    m_inputEdit->installEventFilter(this);

    m_sendButton = new ElaPushButton("Send", inputBar);
    m_sendButton->setObjectName("sendButton");
    m_sendButton->setFixedSize(80, 44);

    inputLayout->addWidget(m_attachButton);
    inputLayout->addWidget(m_inputEdit);
    inputLayout->addWidget(m_sendButton);
    rightLayout->addWidget(inputBar, 0);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    pageLayout->addWidget(splitter);

    connect(m_sendButton, &ElaPushButton::clicked, this, &ChatPage::onSendClicked);
    connect(m_attachButton, &ElaPushButton::clicked, this, &ChatPage::onAttachClicked);
    connect(m_newChatBtn, &ElaPushButton::clicked, this, &ChatPage::newChatRequested);
    connect(m_sessionList, &SessionListWidget::sessionSelected, this, &ChatPage::sessionSelected);
    connect(m_sessionList, &SessionListWidget::sessionDeleteRequested, this, &ChatPage::sessionDeleteRequested);
    connect(m_sessionList, &SessionListWidget::sessionRenameRequested, this, &ChatPage::sessionRenameRequested);
    connect(m_sessionList, &SessionListWidget::sessionExportRequested, this, &ChatPage::sessionExportRequested);
    connect(m_providerCombo, &ElaComboBox::currentTextChanged, this, [this]() {
        emit providerSwitched(m_providerCombo->currentData().toString());
    });
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        // 内容高度变化（新气泡 / 流式追加）后，仅在用户没向上滚浏览历史时
        // 才跟随到底部
        if (m_autoScrollEnabled) {
            QTimer::singleShot(10, this, &ChatPage::scrollToBottom);
        }
    });
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        // 通过当前位置 vs 最大值推断"是否还盯着底部"：阈值 4px 容忍滚动条
        // 把 setValue(maximum) 算为"在底部"以及小幅惯性滚动。
        QScrollBar *bar = m_scrollArea->verticalScrollBar();
        m_autoScrollEnabled = (value >= bar->maximum() - 4);
    });
}

bool ChatPage::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            onSendClicked();
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}

void ChatPage::onAttachClicked()
{
    const QStringList filePaths = QFileDialog::getOpenFileNames(this, "Select attachments");
    for (const QString &filePath : filePaths) {
        addAttachmentFromFile(filePath);
    }
}

void ChatPage::addAttachmentFromFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().trimmed().toLower();
    if (suffix.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    Attachment attachment;
    attachment.fileName = info.fileName();
    attachment.mimeType = mimeTypeForSuffix(suffix);

    if (isImageFile(suffix)) {
        attachment.type = Attachment::Image;
        attachment.fileData = file.readAll();
    } else if (isDocumentFile(suffix)) {
        attachment.type = Attachment::Document;
        attachment.fileData = file.readAll();
    } else {
        attachment.type = Attachment::TextFile;
        attachment.textContent = QString::fromUtf8(file.readAll());
    }

    m_pendingAttachments.append(attachment);
    refreshAttachmentPreview();
}

void ChatPage::refreshAttachmentPreview()
{
    while (m_attachPreviewLayout->count() > 1) {
        QLayoutItem *item = m_attachPreviewLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (m_pendingAttachments.isEmpty()) {
        m_attachPreviewWidget->hide();
        return;
    }

    for (int i = 0; i < m_pendingAttachments.size(); ++i) {
        const Attachment &attachment = m_pendingAttachments.at(i);

        QWidget *chip = new QWidget(m_attachPreviewWidget);
        QHBoxLayout *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(8, 4, 8, 4);
        chipLayout->setSpacing(6);

        QLabel *nameLabel = new QLabel(attachment.fileName, chip);
        chipLayout->addWidget(nameLabel);

        ElaPushButton *removeButton = new ElaPushButton("x", chip);
        removeButton->setFixedSize(20, 20);
        connect(removeButton, &ElaPushButton::clicked, this, [this, i]() {
            if (i < 0 || i >= m_pendingAttachments.size()) {
                return;
            }
            m_pendingAttachments.removeAt(i);
            refreshAttachmentPreview();
        });
        chipLayout->addWidget(removeButton);

        m_attachPreviewLayout->insertWidget(m_attachPreviewLayout->count() - 1, chip);
    }

    m_attachPreviewWidget->show();
}

void ChatPage::clearAttachments()
{
    m_pendingAttachments.clear();
    refreshAttachmentPreview();
}

bool ChatPage::isImageFile(const QString &suffix)
{
    static const QStringList kImageExtensions = {
        "png", "jpg", "jpeg", "gif", "webp", "bmp"
    };
    return kImageExtensions.contains(suffix);
}

bool ChatPage::isDocumentFile(const QString &suffix)
{
    static const QStringList kDocumentExtensions = {
        "pdf"
    };
    return kDocumentExtensions.contains(suffix);
}

QString ChatPage::mimeTypeForSuffix(const QString &suffix)
{
    if (suffix == "png") return "image/png";
    if (suffix == "jpg" || suffix == "jpeg") return "image/jpeg";
    if (suffix == "gif") return "image/gif";
    if (suffix == "webp") return "image/webp";
    if (suffix == "bmp") return "image/bmp";
    if (suffix == "pdf") return "application/pdf";
    return "text/plain";
}

void ChatPage::onSendClicked()
{
    const QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty() && m_pendingAttachments.isEmpty()) {
        return;
    }

    const QList<Attachment> attachments = m_pendingAttachments;
    m_inputEdit->clear();
    clearAttachments();
    emit sendMessageRequested(text, attachments);
}

void ChatPage::addMessageBubble(const QString &role, const QString &content,
                                const QList<Attachment> &attachments,
                                int index, bool favorite)
{
    const int realIndex = (index >= 0) ? index : m_bubbles.size();
    const MessageBubble::Role bubbleRole = (role == "user") ? MessageBubble::User
                                                            : MessageBubble::Assistant;

    auto *bubble = new MessageBubble(bubbleRole, content, attachments, m_messageContainer,
                                     m_userName, m_assistantName,
                                     realIndex, favorite);

    connect(bubble, &MessageBubble::favoriteToggleRequested,
            this, &ChatPage::messageFavoriteToggleRequested);
    connect(bubble, &MessageBubble::deleteFromHereRequested,
            this, &ChatPage::messageDeleteFromHereRequested);
    connect(bubble, &MessageBubble::regenerateRequested,
            this, &ChatPage::messageRegenerateRequested);

    int insertIndex = m_messageLayout->count() - 1;
    if (insertIndex < 0) {
        insertIndex = 0;
    }
    m_messageLayout->insertWidget(insertIndex, bubble);
    m_bubbles.append(bubble);
    scrollToBottom();
}

void ChatPage::appendToLastBubble(const QString &token)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->appendText(token);
    scrollToBottom();
}

void ChatPage::replaceLastBubbleContent(const QString &text)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->setContent(text);
    scrollToBottom();
}

void ChatPage::clearMessages()
{
    for (MessageBubble *bubble : m_bubbles) {
        m_messageLayout->removeWidget(bubble);
        bubble->deleteLater();
    }
    m_bubbles.clear();
    // 切换会话或清空时重置跟随状态，新会话默认从底部开始
    m_autoScrollEnabled = true;
}

void ChatPage::loadMessages(const QList<ChatMessage> &messages)
{
    clearMessages();
    for (int i = 0; i < messages.size(); ++i) {
        const ChatMessage &msg = messages.at(i);
        addMessageBubble(msg.role, msg.content, msg.attachments, i, msg.favorite);
    }
}

void ChatPage::setBubbleFavorite(int index, bool favorite)
{
    for (MessageBubble *bubble : m_bubbles) {
        if (bubble->messageIndex() == index) {
            bubble->setFavorite(favorite);
            return;
        }
    }
}

void ChatPage::setInputEnabled(bool enabled)
{
    m_inputEdit->setEnabled(enabled);
    m_attachButton->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
}

void ChatPage::setStatusText(const QString &text)
{
    if (!m_statusWidget) return;
    if (text.isEmpty()) {
        m_statusWidget->setText("");
        // 没文字也没动画 → 整个 widget 隐藏；动画还在跑则保留可见
        if (!m_statusWidget->isAnimating()) {
            m_statusWidget->hide();
        }
    } else {
        m_statusWidget->setText(text);
        m_statusWidget->show();
    }
}

void ChatPage::setLoading(bool loading)
{
    if (!m_statusWidget) return;
    if (loading) {
        m_statusWidget->show();
        m_statusWidget->start();
    } else {
        m_statusWidget->stop();
        // 没文字就隐藏，有文字（如 "Aborted" / "Error"）保留可见
        if (m_statusWidget->text().isEmpty()) {
            m_statusWidget->hide();
        }
    }
}

void ChatPage::scrollToBottom()
{
    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void ChatPage::addSession(ChatSession *session)
{
    m_sessionList->addSession(session);
}

void ChatPage::removeSession(const QString &id)
{
    m_sessionList->removeSession(id);
}

void ChatPage::setActiveSession(const QString &id)
{
    m_sessionList->setActiveSession(id);
}

void ChatPage::refreshSessionList(const QList<ChatSession *> &sessions)
{
    m_sessionList->refreshList(sessions);
}

void ChatPage::setProviderIndex(int index)
{
    m_providerCombo->setCurrentIndex(index);
}

QString ChatPage::currentProviderData() const
{
    return m_providerCombo->currentData().toString();
}

void ChatPage::updateRoleNames(const QString &userName, const QString &assistantName)
{
    m_userName = userName;
    m_assistantName = assistantName;

    for (MessageBubble *bubble : m_bubbles) {
        bubble->setRoleName(bubble->role() == MessageBubble::User ? userName : assistantName);
    }
}

void ChatPage::refreshPromptTemplates()
{
    if (!m_promptLayout || !m_settings) {
        return;
    }

    while (m_promptLayout->count() > 1) {
        QLayoutItem *item = m_promptLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    QWidget *promptBar = m_promptLayout->parentWidget();
    for (const QString &tmpl : m_settings->promptTemplates()) {
        ElaPushButton *btn = new ElaPushButton(tmpl, promptBar);
        btn->setObjectName("promptTag");
        btn->setFixedHeight(28);
        connect(btn, &ElaPushButton::clicked, this, [this, tmpl]() {
            const QString current = m_inputEdit->toPlainText().trimmed();
            if (current.isEmpty()) {
                m_inputEdit->setPlainText(tmpl + ": ");
            } else {
                m_inputEdit->setPlainText(current + "\n" + tmpl + ": ");
            }

            m_inputEdit->setFocus();
            QTextCursor cursor = m_inputEdit->textCursor();
            cursor.movePosition(QTextCursor::End);
            m_inputEdit->setTextCursor(cursor);
        });
        m_promptLayout->insertWidget(m_promptLayout->count() - 1, btn);
    }
}

// ============================================================================
// 拖拽事件 —— 支持拖拽文件 / 图片到聊天区添加为附件
// ============================================================================

/** @brief 拖入时检查是否包含文件 URL 或图片数据，并触发虚线高亮 */
void ChatPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
        if (!m_dragHighlight) {
            m_dragHighlight = true;
            update();
        }
    }
}

/** @brief 拖动离开窗口：清除虚线高亮 */
void ChatPage::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
    if (m_dragHighlight) {
        m_dragHighlight = false;
        update();
    }
}

/** @brief 拖放释放：遍历所有文件 URL，逐个添加为附件 */
void ChatPage::dropEvent(QDropEvent *event)
{
    if (m_dragHighlight) {
        m_dragHighlight = false;
        update();
    }
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        for (const QUrl &url : mimeData->urls()) {
            if (url.isLocalFile()) {
                addAttachmentFromFile(url.toLocalFile());
            }
        }
        event->acceptProposedAction();
    } else if (mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            addAttachmentFromImage(image);
            event->acceptProposedAction();
        }
    }
}

/** @brief 拖拽进行时在 ChatPage 边缘画一圈虚线，提示用户可以释放附件 */
void ChatPage::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (!m_dragHighlight) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#10a37f"), 3, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // 内缩 4px 让虚线完整可见，不被父布局裁掉
    p.drawRoundedRect(rect().adjusted(4, 4, -4, -4), 8, 8);
}

/**
 * @brief 把内存中的 QImage（剪贴板粘贴 / 拖入图片数据）添加为图片附件
 *
 * 没有文件路径，文件名固定为 "clipboard.png"。超过 2048px 的边按比例缩小，
 * 编码成 PNG 后写入 m_pendingAttachments。
 */
void ChatPage::addAttachmentFromImage(const QImage &image)
{
    Attachment att;
    att.type = Attachment::Image;
    att.fileName = "clipboard.png";
    att.mimeType = "image/png";

    QImage img = image;
    if (img.width() > 2048 || img.height() > 2048) {
        img = img.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QBuffer buffer(&att.fileData);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    m_pendingAttachments.append(att);
    refreshAttachmentPreview();
}
