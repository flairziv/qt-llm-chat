#include "ChatPage.h"

#include "MessageBubble.h"
#include "SessionListWidget.h"
#include "AssistantLoadingWidget.h"
#include "core/AppSettings.h"

#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStringList>
#include <QTextCursor>
#include <QTimer>
#include <QWheelEvent>

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

    // Ctrl+F 触发的搜索栏（默认隐藏，置于 scrollArea 上方）
    m_searchBar = new QWidget(rightPanel);
    m_searchBar->setObjectName("searchBar");
    m_searchBar->hide();
    auto *searchLayout = new QHBoxLayout(m_searchBar);
    searchLayout->setContentsMargins(16, 4, 16, 4);
    searchLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText("Search in messages...");
    searchLayout->addWidget(m_searchEdit, 1);

    auto *searchCloseBtn = new ElaPushButton("x", m_searchBar);
    searchCloseBtn->setFixedSize(24, 24);
    searchLayout->addWidget(searchCloseBtn);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.isEmpty()) {
            clearSearchHighlight();
        } else {
            searchInMessages(text);
        }
    });
    connect(searchCloseBtn, &ElaPushButton::clicked, this, [this]() {
        clearSearchHighlight();
        m_searchBar->hide();
    });

    rightLayout->addWidget(m_searchBar, 0);
    rightLayout->addWidget(m_scrollArea, 1);

    // 流式时显示眨眼/扫视头像 + 状态文字，setStatusText / setLoading 控制
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
    m_inputEdit->setMinimumHeight(44);
    m_inputEdit->setMaximumHeight(120);
    m_inputEdit->setFixedHeight(44);
    m_inputEdit->setAcceptDrops(false); // 拖拽统一由 ChatPage 处理，避免事件被 QTextEdit 吞掉
    m_inputEdit->installEventFilter(this);

    // 输入框跟随内容自适应高度：单行时 44px、多行时按文档高度增长，封顶 120px。
    // +12 是文档高度到 QTextEdit 实际高度的视觉补偿（边距 + 滚动条预留）。
    connect(m_inputEdit, &QTextEdit::textChanged, this, [this]() {
        const int docHeight = static_cast<int>(m_inputEdit->document()->size().height()) + 12;
        const int newHeight = qBound(44, docHeight, 120);
        m_inputEdit->setFixedHeight(newHeight);
    });

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

    // 集中监听主题切换：把所有气泡的主题相关样式重刷一遍。
    // 不让每个 bubble 自己连 eTheme，避免大量气泡创建/销毁时连接列表震荡。
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this]() {
        for (auto *bubble : m_bubbles) {
            if (bubble) bubble->rerenderForTheme();
        }
        // 输入框 / 搜索框的 placeholder 颜色不是 QSS 控的，是 QPalette::PlaceholderText
        // 角色。切到夜间时如果不重刷，会留着白天那种灰，在深背景上几乎看不见。
        const QColor c = qApp->palette().color(QPalette::PlaceholderText);
        for (QWidget *w : { static_cast<QWidget *>(m_inputEdit),
                            static_cast<QWidget *>(m_searchEdit) }) {
            if (!w) continue;
            QPalette p = w->palette();
            p.setColor(QPalette::PlaceholderText, c);
            w->setPalette(p);
        }
    });

    // Ctrl+= / Ctrl++ 放大；Ctrl+- 缩小；Ctrl+0 复位。Ctrl+wheel 走 wheelEvent。
    auto *zoomInSc = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Equal), this);
    connect(zoomInSc, &QShortcut::activated, this, &ChatPage::zoomIn);
    auto *zoomInSc2 = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Plus), this);
    connect(zoomInSc2, &QShortcut::activated, this, &ChatPage::zoomIn);
    auto *zoomOutSc = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Minus), this);
    connect(zoomOutSc, &QShortcut::activated, this, &ChatPage::zoomOut);
    auto *zoomResetSc = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_0), this);
    connect(zoomResetSc, &QShortcut::activated, this, &ChatPage::zoomReset);

    // Ctrl+F 打开消息搜索栏
    auto *searchSc = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_F), this);
    connect(searchSc, &QShortcut::activated, this, [this]() {
        m_searchBar->show();
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });

    // 首次 palette 同步：themeModeChanged 只在用户切主题时触发，启动时也得跑一次
    // 才能让夜间模式下首次显示的 placeholder 颜色是正确的。
    {
        const QColor c = qApp->palette().color(QPalette::PlaceholderText);
        for (QWidget *w : { static_cast<QWidget *>(m_inputEdit),
                            static_cast<QWidget *>(m_searchEdit) }) {
            if (!w) continue;
            QPalette p = w->palette();
            p.setColor(QPalette::PlaceholderText, c);
            w->setPalette(p);
        }
    }
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

        // Ctrl+V：图片 / 文件 URL 直接吸成附件，纯文本仍走 QTextEdit 默认粘贴。
        // 截图工具大多只放 imageData，不放 urls；从文件管理器复制的文件则只有 urls；
        // 两条都要单独处理。处理失败（image.isNull / no local file）就 return false，
        // 让默认粘贴尝试当作文本贴入，避免空 Ctrl+V 没反应。
        if (keyEvent->key() == Qt::Key_V && (keyEvent->modifiers() & Qt::ControlModifier)) {
            const QClipboard *clipboard = QApplication::clipboard();
            const QMimeData *mimeData = clipboard->mimeData();
            if (mimeData && mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                if (!image.isNull()) {
                    addAttachmentFromImage(image);
                    return true;
                }
            }
            if (mimeData && mimeData->hasUrls()) {
                bool hasFile = false;
                for (const QUrl &url : mimeData->urls()) {
                    if (url.isLocalFile()) {
                        addAttachmentFromFile(url.toLocalFile());
                        hasFile = true;
                    }
                }
                if (hasFile) return true;
            }
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
                                int index, bool favorite,
                                const QDateTime &timestamp,
                                const QString &reasoning)
{
    const int realIndex = (index >= 0) ? index : m_bubbles.size();
    const MessageBubble::Role bubbleRole = (role == "user") ? MessageBubble::User
                                                            : MessageBubble::Assistant;

    auto *bubble = new MessageBubble(bubbleRole, content, attachments, m_messageContainer,
                                     m_userName, m_assistantName,
                                     realIndex, favorite, timestamp, reasoning);

    // 让新气泡跟随当前缩放级别（默认 14 时跳过 rebuild，省一次图片缩放）
    if (m_fontSize != kDefaultFontSize) {
        bubble->setContentFontSize(m_fontSize);
    }

    // 气泡最大宽度跟随聊天区域：viewport 宽 - 40 (左右各 20px 边距) 的 75%，最小 300px
    const int areaWidth = m_scrollArea->viewport()->width() - 40;
    bubble->setMaxBubbleWidth(qMax(areaWidth * 3 / 4, 300));

    connect(bubble, &MessageBubble::favoriteToggleRequested,
            this, &ChatPage::messageFavoriteToggleRequested);
    connect(bubble, &MessageBubble::deleteFromHereRequested,
            this, &ChatPage::messageDeleteFromHereRequested);
    connect(bubble, &MessageBubble::regenerateRequested,
            this, &ChatPage::messageRegenerateRequested);
    connect(bubble, &MessageBubble::editRequested,
            this, &ChatPage::messageEditRequested);

    int insertIndex = m_messageLayout->count() - 1;
    if (insertIndex < 0) {
        insertIndex = 0;
    }
    m_messageLayout->insertWidget(insertIndex, bubble);
    m_bubbles.append(bubble);
    // 新气泡 = 用户主动场景（发送 / 加载会话），强制滚到底部并重启跟随
    scrollToBottomForce();
}

void ChatPage::appendToLastBubble(const QString &token)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->appendText(token);
    scrollToBottom();
}

void ChatPage::appendReasoningToLastBubble(const QString &token)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->appendReasoning(token);
    scrollToBottom();
}

void ChatPage::addToolDataToLastBubble(const QList<ToolCall> &calls,
                                       const QList<ToolResult> &results)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->setToolData(calls, results);
    scrollToBottom();
}

void ChatPage::addImagePlaceholderToLastBubble(int count)
{
    if (m_bubbles.isEmpty()) return;
    for (int i = 0; i < qMax(1, count); ++i) {
        m_bubbles.last()->addImagePlaceholder();
    }
    scrollToBottom();
}

void ChatPage::clearImagePlaceholdersInLastBubble()
{
    if (m_bubbles.isEmpty()) return;
    m_bubbles.last()->clearImagePlaceholders();
}

void ChatPage::replaceLastBubbleContent(const QString &text)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->setContent(text);
    scrollToBottom();
}

void ChatPage::replaceLastBubbleMessage(const QString &text, const QList<Attachment> &attachments)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    // 先换附件再换正文：setAttachments 会重建附件区子 widget；setContent 只动 QLabel。
    // 两次都会触发布局，把它放一起完成避免中间态闪烁。
    m_bubbles.last()->setAttachments(attachments);
    m_bubbles.last()->setContent(text);
    scrollToBottom();
}

void ChatPage::replaceLastBubbleReasoning(const QString &reasoning)
{
    if (m_bubbles.isEmpty()) {
        return;
    }
    m_bubbles.last()->setReasoning(reasoning);
}

void ChatPage::searchInMessages(const QString &keyword)
{
    // 命中样式直接 setStyleSheet 在 MessageBubble 外层，#userBubble / #assistantBubble
    // 选择器走级联匹配到内层气泡；favorite 是用 dynamic property 触发的 QSS 规则，
    // 这里的内联样式会临时压住它，clearSearchHighlight() 一并清除恢复正常。
    MessageBubble *firstMatch = nullptr;
    for (auto *bubble : m_bubbles) {
        if (bubble->content().contains(keyword, Qt::CaseInsensitive)) {
            bubble->setStyleSheet("QWidget#userBubble, QWidget#assistantBubble { "
                                  "border: 2px solid #ffd700; }");
            if (!firstMatch) firstMatch = bubble;
        } else {
            bubble->setStyleSheet("");
        }
    }
    if (firstMatch) {
        m_scrollArea->ensureWidgetVisible(firstMatch);
    }
}

void ChatPage::clearSearchHighlight()
{
    for (auto *bubble : m_bubbles) {
        bubble->setStyleSheet("");
    }
}

/**
 * @brief 删除并销毁最末一条气泡
 *
 * 主要用途：流式开始时预建的空 assistant 气泡，在 Esc 中止且未收到任何
 * token 时会成为孤儿（session 里没有对应消息），需要主动清掉避免 UI 与
 * 持久化状态错位。空列表时安全 no-op。
 */
void ChatPage::removeLastBubble()
{
    if (m_bubbles.isEmpty()) return;
    MessageBubble *last = m_bubbles.takeLast();
    m_messageLayout->removeWidget(last);
    last->deleteLater();
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

        // 合成的 tool_result 消息（role=user、只含 toolResults、无正文）不单独成气泡：
        // 它的结果会合并进上一条 assistant(tool_use) 气泡的工具区块，这里跳过，
        // 否则会渲染出一个空的右对齐 "You" 气泡。
        if (msg.role == "user" && !msg.toolResults.isEmpty() && msg.content.isEmpty()) {
            continue;
        }

        addMessageBubble(msg.role, msg.content, msg.attachments, i, msg.favorite,
                         msg.timestamp, msg.reasoning);

        // assistant 本轮发起过 tool_use：把下一条消息里配对的 tool_result 一并挂上，
        // 调用与结果在同一个气泡里成对展示。
        if (msg.role == "assistant" && !msg.toolCalls.isEmpty()) {
            QList<ToolResult> results;
            if (i + 1 < messages.size()) {
                results = messages.at(i + 1).toolResults;
            }
            m_bubbles.last()->setToolData(msg.toolCalls, results);
        }
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

/** @brief 将文本回填到输入框、聚焦、光标移到末尾。由编辑消息流程调用 */
void ChatPage::fillInputText(const QString &text)
{
    m_inputEdit->setPlainText(text);
    m_inputEdit->setFocus();
    QTextCursor cursor = m_inputEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_inputEdit->setTextCursor(cursor);
}

void ChatPage::scrollToBottomForce()
{
    // 强制滚到底部并重启自动跟随。用户主动场景（发送消息、切换会话、加载历史）走这里；
    // 流式 token 不要走，否则用户向上滚看历史会被新 token 拉回来。
    m_autoScrollEnabled = true;
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

void ChatPage::moveSessionToTop(const QString &id)
{
    m_sessionList->moveSessionToTop(id);
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
 * @brief Ctrl+滚轮：向上放大，向下缩小；其它修饰键交给父类正常滚动消息区。
 */
void ChatPage::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else if (event->angleDelta().y() < 0) {
            zoomOut();
        }
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

/** @brief 窗口大小变化时把新的最大气泡宽度推送给所有现有气泡 */
void ChatPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateBubbleMaxWidth();
}

/**
 * @brief 按聊天区域视窗宽度 75% 计算气泡最大宽度，应用到所有气泡
 *
 * 视窗宽度先减去消息容器左右边距各 20px，再取 75%；最小 300px 兜底，避免
 * 窄窗里气泡缩成一线。新建气泡时 addMessageBubble 也会单独算一次同值。
 */
void ChatPage::updateBubbleMaxWidth()
{
    const int areaWidth = m_scrollArea->viewport()->width() - 40;
    const int maxWidth = qMax(areaWidth * 3 / 4, 300);
    for (auto *bubble : m_bubbles) {
        bubble->setMaxBubbleWidth(maxWidth);
    }
}

/** @brief 放大字体（步进 2px，上限 28px） */
void ChatPage::zoomIn()
{
    if (m_fontSize < kMaxFontSize) {
        m_fontSize += 2;
        applyFontSizeToAllBubbles();
    }
}

/** @brief 缩小字体（步进 2px，下限 10px） */
void ChatPage::zoomOut()
{
    if (m_fontSize > kMinFontSize) {
        m_fontSize -= 2;
        applyFontSizeToAllBubbles();
    }
}

/** @brief 恢复默认字号 14px */
void ChatPage::zoomReset()
{
    m_fontSize = kDefaultFontSize;
    applyFontSizeToAllBubbles();
}

/** @brief 把当前 m_fontSize 推送到所有已有气泡（zoomXxx 调用） */
void ChatPage::applyFontSizeToAllBubbles()
{
    for (auto *bubble : m_bubbles) {
        bubble->setContentFontSize(m_fontSize);
    }
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
