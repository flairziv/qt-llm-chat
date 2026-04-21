#include "ChatPage.h"
#include "MessageBubble.h"
#include "SessionListWidget.h"
#include "core/ChatSession.h"
#include "core/AppSettings.h"
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <QScrollBar>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTimer>

// ============================================================================
// 构造函数
// ============================================================================

ChatPage::ChatPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setupUI();
    refreshPromptTemplates();
    if (m_settings) {
        connect(m_settings, &AppSettings::promptTemplatesChanged,
                this, &ChatPage::refreshPromptTemplates);
    }
}

// ============================================================================
// UI 构建
// ============================================================================

/**
 * @brief 构建聊天页面的完整 UI 布局
 *
 * 布局层级（从外到内）：
 *
 *   ChatPage (QHBoxLayout)
 *   └── QSplitter (水平方向)
 *       ├── leftPanel (固定宽度 260px)
 *       │   └── QVBoxLayout
 *       │       ├── headerLayout (QHBoxLayout)
 *       │       │   ├── m_providerCombo  ← Provider 选择
 *       │       │   └── m_newChatBtn     ← "+ New" 按钮
 *       │       └── m_sessionList        ← 会话列表（stretch=1，占满剩余空间）
 *       │
 *       └── rightPanel (自适应拉伸)
 *           └── QVBoxLayout
 *               ├── m_scrollArea (stretch=1，占满空间)
 *               │   └── m_messageContainer
 *               │       └── m_messageLayout (VBox + 底部 stretch)
 *               │           ├── MessageBubble ...
 *               │           └── <stretch> ← 气泡少时将内容推到顶部
 *               ├── m_statusLabel (隐藏，仅错误/加载时显示)
 *               └── inputBar (固定高度)
 *                   ├── m_inputEdit   ← 输入框
 *                   └── m_sendButton  ← "Send" 按钮
 */
void ChatPage::setupUI()
{
    // 最外层布局：无边距，由 QSplitter 填满
    QHBoxLayout *pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    // QSplitter：左右两栏可拖拽分隔
    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);  // 禁止拖拽到完全折叠
    splitter->setHandleWidth(1);              // 分隔条宽度 1px

    // ===================== 左侧面板 =====================
    QWidget *leftPanel = new QWidget;
    leftPanel->setObjectName("leftPanel");
    leftPanel->setFixedWidth(260);            // 固定宽度，不随窗口缩放

    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(8);

    // 顶部横排：Provider 下拉框 + 新建聊天按钮
    QHBoxLayout *headerLayout = new QHBoxLayout;
    m_providerCombo = new ElaComboBox;
    m_providerCombo->setObjectName("providerCombo");
    m_providerCombo->addItem("Claude", "claude");   // 显示文本, userData
    m_providerCombo->addItem("OpenAI", "openai");

    m_newChatBtn = new ElaPushButton("+ New");
    m_newChatBtn->setObjectName("newChatButton");

    headerLayout->addWidget(m_providerCombo, 1);  // stretch=1，占满剩余空间
    headerLayout->addWidget(m_newChatBtn);
    leftLayout->addLayout(headerLayout);

    // 会话列表（stretch=1，占满左侧面板剩余空间）
    m_sessionList = new SessionListWidget;
    leftLayout->addWidget(m_sessionList, 1);

    // ===================== 右侧面板 =====================
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // --- 消息滚动区域 ---
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);     // 内部 widget 自动适应滚动区宽度
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setObjectName("chatScrollArea");
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 禁用水平滚动
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 消息容器：放置所有 MessageBubble
    m_messageContainer = new QWidget;
    m_messageContainer->setObjectName("messageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setSpacing(12);                     // 气泡间距
    m_messageLayout->setContentsMargins(20, 20, 20, 20);
    m_messageLayout->addStretch(1);  // 底部弹簧：消息少时将气泡推到顶部

    m_scrollArea->setWidget(m_messageContainer);
    rightLayout->addWidget(m_scrollArea, 1);  // stretch=1，占满右侧主体

    // --- 状态标签（默认隐藏） ---
    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->hide();
    rightLayout->addWidget(m_statusLabel);

    // --- Prompt 模板标签栏 ---
    QWidget *promptBar = new QWidget(rightPanel);
    promptBar->setObjectName("promptBar");
    m_promptLayout = new QHBoxLayout(promptBar);
    m_promptLayout->setContentsMargins(16, 4, 16, 0);
    m_promptLayout->setSpacing(6);
    m_promptLayout->addStretch();
    rightLayout->addWidget(promptBar, 0);

    // --- 输入栏 ---
    QWidget *inputBar = new QWidget;
    inputBar->setObjectName("inputBar");
    inputBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout *inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(16, 10, 16, 10);

    m_inputEdit = new QTextEdit;
    m_inputEdit->setObjectName("chatInput");
    m_inputEdit->setPlaceholderText("Type a message... (Enter to send, Shift+Enter for newline)");
    m_inputEdit->setMaximumHeight(120);
    m_inputEdit->setFixedHeight(44);
    m_inputEdit->installEventFilter(this);  // 安装事件过滤器以拦截 Enter 键

    m_sendButton = new ElaPushButton("Send");
    m_sendButton->setObjectName("sendButton");
    m_sendButton->setFixedSize(80, 44);

    inputLayout->addWidget(m_inputEdit);
    inputLayout->addWidget(m_sendButton);
    rightLayout->addWidget(inputBar, 0);  // stretch=0，固定在底部

    // ===================== 组装 Splitter =====================
    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);  // 左侧不拉伸
    splitter->setStretchFactor(1, 1);  // 右侧自适应拉伸
    pageLayout->addWidget(splitter);

    // ===================== 信号连接 =====================
    connect(m_sendButton, &ElaPushButton::clicked, this, &ChatPage::onSendClicked);
    connect(m_newChatBtn, &ElaPushButton::clicked, this, &ChatPage::newChatRequested);
    connect(m_sessionList, &SessionListWidget::sessionSelected, this, &ChatPage::sessionSelected);
    connect(m_sessionList, &SessionListWidget::sessionDeleteRequested, this, &ChatPage::sessionDeleteRequested);
    // Provider 切换时发射信号（传递 userData 而非显示文本）
    connect(m_providerCombo, &ElaComboBox::currentTextChanged, this, [this]() {
        emit providerSwitched(m_providerCombo->currentData().toString());
    });

    // 消息区内容变化时自动滚动到底部（延迟 10ms 等待布局刷新）
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() {
        QTimer::singleShot(10, this, &ChatPage::scrollToBottom);
    });
}

// ============================================================================
// 事件过滤器 —— Enter 键发送消息
// ============================================================================

/**
 * @brief 拦截输入框的键盘事件
 *
 * - Enter / Return：触发发送（等效点击 Send 按钮）
 * - Shift + Enter：插入换行（默认行为，不拦截）
 */
bool ChatPage::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                onSendClicked();
                return true;  // 事件已处理，阻止默认换行
            }
        }
    }
    return QWidget::eventFilter(obj, event);  // 其他事件交给父类处理
}

/**
 * @brief 发送按钮点击处理：提取文本、清空输入框、发射信号
 */
void ChatPage::onSendClicked()
{
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;  // 空消息不发送
    m_inputEdit->clear();
    emit sendMessageRequested(text);
}

// ============================================================================
// 消息管理
// ============================================================================

/**
 * @brief 添加一个新的消息气泡到消息区
 *
 * 新气泡插入到 m_messageLayout 的倒数第二个位置（最后一个是 stretch），
 * 这样气泡始终紧贴顶部排列，底部弹簧负责填充剩余空间。
 */
void ChatPage::addMessageBubble(const QString &role, const QString &content)
{
    MessageBubble::Role bubbleRole = (role == "user") ? MessageBubble::User : MessageBubble::Assistant;
    auto *bubble = new MessageBubble(bubbleRole, content, m_messageContainer,
                                     m_userName, m_assistantName);

    // 插入到 stretch 之前（count-1 是 stretch 的位置）
    int idx = m_messageLayout->count() - 1;
    if (idx < 0) idx = 0;
    m_messageLayout->insertWidget(idx, bubble);
    m_bubbles.append(bubble);
    scrollToBottom();
}

/**
 * @brief 向最后一个气泡追加文本（用于流式 token 的逐步填充）
 */
void ChatPage::appendToLastBubble(const QString &token)
{
    if (m_bubbles.isEmpty()) return;
    m_bubbles.last()->appendText(token);
    scrollToBottom();
}

/**
 * @brief 替换最后一个气泡的完整文本（用于流式结束后去除情绪标签等）
 */
void ChatPage::replaceLastBubbleContent(const QString &text)
{
    if (m_bubbles.isEmpty()) return;
    m_bubbles.last()->setContent(text);
    scrollToBottom();
}

/**
 * @brief 清空所有消息气泡，释放内存
 */
void ChatPage::clearMessages()
{
    for (auto *bubble : m_bubbles) {
        m_messageLayout->removeWidget(bubble);
        bubble->deleteLater();
    }
    m_bubbles.clear();
}

/**
 * @brief 批量加载消息记录（切换会话时调用）
 */
void ChatPage::loadMessages(const QList<ChatMessage> &messages)
{
    clearMessages();
    for (const auto &msg : messages) {
        addMessageBubble(msg.role, msg.content);
    }
}

// ============================================================================
// UI 状态控制
// ============================================================================

/** @brief 启用/禁用输入框和发送按钮（流式请求期间禁用） */
void ChatPage::setInputEnabled(bool enabled)
{
    m_inputEdit->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
}

/** @brief 设置/隐藏状态文本（空字符串隐藏，非空显示） */
void ChatPage::setStatusText(const QString &text)
{
    if (text.isEmpty()) {
        m_statusLabel->hide();
    } else {
        m_statusLabel->setText(text);
        m_statusLabel->show();
    }
}

/** @brief 将消息滚动区滚动到最底部 */
void ChatPage::scrollToBottom()
{
    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    bar->setValue(bar->maximum());
}

// ============================================================================
// 会话列表代理 —— 简单转发给 SessionListWidget
// ============================================================================

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

void ChatPage::refreshSessionList(const QList<ChatSession*> &sessions)
{
    m_sessionList->refreshList(sessions);
}

// ============================================================================
// Provider 下拉框代理
// ============================================================================

void ChatPage::setProviderIndex(int index)
{
    m_providerCombo->setCurrentIndex(index);
}

QString ChatPage::currentProviderData() const
{
    return m_providerCombo->currentData().toString();
}

/**
 * @brief 更新显示名称，同时刷新所有已有气泡的角色标签
 */
void ChatPage::updateRoleNames(const QString &userName, const QString &assistantName)
{
    m_userName = userName;
    m_assistantName = assistantName;
    for (MessageBubble *b : m_bubbles) {
        b->setRoleName(b->role() == MessageBubble::User ? userName : assistantName);
    }
}

/** @brief 重新加载 Prompt 模板栏（从 AppSettings 读取） */
void ChatPage::refreshPromptTemplates()
{
    if (!m_promptLayout || !m_settings) return;

    // 移除旧按钮（保留末尾的 stretch）
    while (m_promptLayout->count() > 1) {
        QLayoutItem *item = m_promptLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QWidget *promptBar = m_promptLayout->parentWidget();
    for (const QString &tmpl : m_settings->promptTemplates()) {
        ElaPushButton *btn = new ElaPushButton(tmpl, promptBar);
        btn->setObjectName("promptTag");
        btn->setFixedHeight(28);
        connect(btn, &ElaPushButton::clicked, this, [this, tmpl]() {
            QString current = m_inputEdit->toPlainText().trimmed();
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
