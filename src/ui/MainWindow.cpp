#include "MainWindow.h"
#include "ChatPage.h"
#include "AnalyticsPage.h"
#include "AsciiArtPage.h"
#include "SettingClaudePage.h"
#include "SettingGeminiPage.h"
#include "SettingOpenAIPage.h"
#include "SettingGeneralPage.h"
#include "TachieWindow.h"
#include "core/AppSettings.h"
#include "core/SessionManager.h"
#include "core/ClaudeProvider.h"
#include "core/GeminiProvider.h"
#include "core/OpenAIProvider.h"
#include "core/ChatSession.h"
#include "core/EdgeTTSProvider.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QMediaPlayer>
#include <QDebug>
#include <QTimer>
#include <QVBoxLayout>
#include <QRandomGenerator>

#include "ElaContentDialog.h"

// ============================================================================
// 构造 / 析构
// ============================================================================

MainWindow::MainWindow(AppSettings *settings, QWidget *parent)
    : ElaWindow(parent)
    , m_settings(settings)
    , m_nam(new QNetworkAccessManager(this))
    , m_sessionManager(new SessionManager(settings->dataDir(), this))
{
    setupPages();       // 1. 创建页面和导航树
    setupConnections(); // 2. 建立信号槽
    createProvider();   // 3. 根据设置创建 LLM Provider
    loadSessions();     // 4. 从磁盘加载历史会话
    setupTachie();      // 5. 初始化立绘窗口

    // 6. 初始化 TTS（MP3 播放：合成完成后播放完整文件）
    m_ttsProvider = new EdgeTTSProvider(this);
    m_ttsPlayer = new QMediaPlayer(this);

    // 合成完成 → 设置 flag 并播放
    connect(m_ttsProvider, &EdgeTTSProvider::synthesisFinished,
            this, [this](const QString &audioFilePath) {
        // qDebug() << "[TTS] Playing audio file:" << audioFilePath;
        m_ttsPendingPlay = true;
        m_ttsPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(audioFilePath)));
        m_ttsPlayer->play();
    });

    // 播放结束 → 清 flag 和 media，彻底防止窗口聚焦时重播
    connect(m_ttsPlayer, &QMediaPlayer::stateChanged,
            this, [this](QMediaPlayer::State state) {
        if (state == QMediaPlayer::StoppedState && m_ttsPendingPlay) {
            m_ttsPendingPlay = false;
            m_ttsPlayer->setMedia(QMediaContent());
        }
        // 嘴型同步：播放时启动计时器，停止时停止
        if (state == QMediaPlayer::PlayingState && m_tachieWindow && m_lipSyncTimer) {
            m_lipSyncEmotion = m_tachieWindow->currentEmotion();
            m_lipSyncTimer->start(200);
        } else if (state == QMediaPlayer::StoppedState && m_lipSyncTimer && m_lipSyncTimer->isActive()) {
            m_lipSyncTimer->stop();
            if (m_tachieWindow && !m_lipSyncEmotion.isEmpty()) {
                m_tachieWindow->changeExpression(m_lipSyncEmotion);
            }
        }
    });

    // 嘴型同步计时器：每 200ms 随机切换表情（需要立绘资源支持）
    m_lipSyncTimer = new QTimer(this);
    connect(m_lipSyncTimer, &QTimer::timeout, this, [this]() {
        if (!m_tachieWindow) return;
        QStringList emotions = m_tachieWindow->availableEmotions();
        // 优先使用 "说话"/"兴奋" 表情模拟嘴型，否则在现有表情中选两个切换
        static const QStringList mouthEmotions = {"说话", "兴奋", "高兴", "正常"};
        QStringList available;
        for (const QString &e : mouthEmotions) {
            if (emotions.contains(e)) available.append(e);
        }
        if (available.size() < 2) return;  // 不够 2 个表情无法模拟
        int idx = QRandomGenerator::global()->bounded(available.size());
        m_tachieWindow->changeExpression(available[idx]);
    });

    // 流式 flush 节流计时器：80ms 单次触发；onTokenReceived 把 token 累积到
    // m_pendingBubbleText，等下一次触发时一次性喂给气泡。33ms (~30fps) 在长
    // 回复里 QLabel relayout 跑满，80ms (~12fps) 视觉无差别但 layout 工作量
    // 减约 60%。
    m_streamFlushTimer = new QTimer(this);
    m_streamFlushTimer->setSingleShot(true);
    m_streamFlushTimer->setInterval(80);
    connect(m_streamFlushTimer, &QTimer::timeout, this, &MainWindow::flushPendingBubbleText);

    // TTS 错误日志
    connect(m_ttsProvider, &EdgeTTSProvider::errorOccurred,
            this, [](const QString &err) {
        qWarning() << "[TTS] Error:" << err;
    });

    // TTS 启用时预建立 WebSocket 连接，消除首次合成的建连延迟
    if (m_settings->ttsEnabled()) {
        m_ttsProvider->preConnect();
    }
}

MainWindow::~MainWindow()
{
    // 析构时中止可能正在进行的流式请求
    if (m_provider) {
        m_provider->abort();
    }
    // 停止 TTS
    m_ttsProvider->abort();
    m_ttsPlayer->stop();
    // 保存立绘窗口位置并销毁
    if (m_tachieWindow) {
        m_settings->setTachiePositionX(m_tachieWindow->x());
        m_settings->setTachiePositionY(m_tachieWindow->y());
        m_tachieWindow->close();
    }
}

// ============================================================================
// 页面初始化 —— 构建 ElaWindow 导航树
// ============================================================================

/**
 * @brief 创建所有页面并注册到 ElaWindow 的导航树
 *
 * ElaWindow 导航结构：
 *   ├── Chat（主页面，House 图标）         ← addPageNode，一级节点
 *   └── Settings（可展开分组）              ← addExpanderNode，分组节点
 *       ├── Claude（Claude 设置页）         ← addPageNode，二级节点
 *       ├── OpenAI（OpenAI 设置页）
 *       └── General（通用设置页）
 */
void MainWindow::setupPages()
{
    setWindowTitle("LLMChat");
    resize(1000, 700);
    setWindowIcon(QIcon(":/imgs/favicon.ico"));
    setUserInfoCardPixmap(QPixmap(":/imgs/favicon.ico"));
    setUserInfoCardTitle("LLMChat");
    setUserInfoCardSubTitle("flairziv@gmail.com");
    // 背景图：优先加载本地路径，为空或加载失败则用内置资源
    QString pixmapPath = m_settings->backgroundPixmapPath();
    QPixmap bgPixmap(pixmapPath);
    if (bgPixmap.isNull())
        bgPixmap = QPixmap(":/imgs/Miku.png");
    setWindowPixmap(ElaThemeType::Light, bgPixmap);

    QString moviePath = m_settings->backgroundMoviePath();
    if (moviePath.isEmpty())
        moviePath = ":/imgs/Miku.gif";
    setWindowMoviePath(ElaThemeType::Light, moviePath);

    setWindowPaintMode(static_cast<ElaWindowType::PaintMode>(m_settings->backgroundPaintMode()));
    setIsCentralStackedWidgetTransparent(true);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, true);

    // 聊天主页面 —— 一级导航节点
    m_chatPage = new ChatPage(m_settings, this);
    m_chatPage->updateRoleNames(m_settings->userName(), m_settings->assistantName());
    addPageNode("Chat", m_chatPage, ElaIconType::House);

    // 根据持久化设置恢复上次选择的 Provider
    QString activeProvider = m_settings->activeProvider();
    int idx = (activeProvider == "openai") ? 1 : (activeProvider == "gemini") ? 2 : 0;
    m_chatPage->setProviderIndex(idx);

    // 会话分析页 —— 统计所有会话的数据
    m_analyticsPage = new AnalyticsPage(m_sessionManager, this);
    addPageNode("Analytics", m_analyticsPage, ElaIconType::ChartArea);

    // ASCII Art 工具页 —— 把图片转字符画
    m_asciiArtPage = new AsciiArtPage(this);
    addPageNode("ASCII Art", m_asciiArtPage, ElaIconType::Image);

    // 设置页分组 —— 可展开的二级导航
    QString settingsGroup;
    addExpanderNode("Settings", settingsGroup, ElaIconType::GingerbreadMan);

    m_claudePage = new SettingClaudePage(m_settings, this);
    addPageNode("Claude", m_claudePage, settingsGroup, ElaIconType::UserRobot);

    m_openaiPage = new SettingOpenAIPage(m_settings, this);
    addPageNode("OpenAI", m_openaiPage, settingsGroup, ElaIconType::Snowman);

    m_geminiPage = new SettingGeminiPage(m_settings, this);
    addPageNode("Gemini", m_geminiPage, settingsGroup, ElaIconType::Diamond);

    m_generalPage = new SettingGeneralPage(m_settings, this);
    addPageNode("General", m_generalPage, settingsGroup, ElaIconType::Speaker);
}

// ============================================================================
// 信号槽连接 —— 将 UI 操作绑定到业务逻辑
// ============================================================================

/**
 * @brief 建立所有信号槽连接
 *
 * 信号流向：
 *   ChatPage  ──sendMessageRequested──→  MainWindow::onSendMessage
 *                                           ↓ 调用 LLMProvider
 *   LLMProvider ──tokenReceived──────→  MainWindow::onTokenReceived
 *                                           ↓ 转发给 ChatPage
 *   ChatPage  ←──appendToLastBubble──
 *
 *   AppSettings ──providerSettingsChanged──→ MainWindow::onProviderSettingsChanged
 *                                              ↓ 重建 Provider
 *   AppSettings ──uiSettingsChanged────────→ MainWindow::onUiSettingsChanged
 *                                              ↓ 刷新角色名 / 立绘资源目录
 *
 *   SessionManager ──sessionCreated/Deleted/Changed──→ ChatPage 列表刷新
 */
void MainWindow::setupConnections()
{
    // === ChatPage 用户操作信号 ===
    connect(m_chatPage, &ChatPage::sendMessageRequested, this, &MainWindow::onSendMessage);
    connect(m_chatPage, &ChatPage::newChatRequested, this, &MainWindow::onNewChat);
    connect(m_chatPage, &ChatPage::sessionSelected, this, &MainWindow::onSessionSelected);
    connect(m_chatPage, &ChatPage::sessionDeleteRequested, this, &MainWindow::onDeleteChat);
    connect(m_chatPage, &ChatPage::sessionRenameRequested, this, &MainWindow::onRenameChat);
    connect(m_chatPage, &ChatPage::sessionExportRequested, this, &MainWindow::onExportSession);
    connect(m_chatPage, &ChatPage::providerSwitched, this, &MainWindow::onProviderSwitched);
    connect(m_chatPage, &ChatPage::messageFavoriteToggleRequested,
            this, &MainWindow::onMessageFavoriteToggle);
    connect(m_chatPage, &ChatPage::messageDeleteFromHereRequested,
            this, &MainWindow::onMessageDeleteFromHere);
    connect(m_chatPage, &ChatPage::messageRegenerateRequested,
            this, &MainWindow::onMessageRegenerate);

    // === 设置变更信号 ===
    // 旧路径是「SettingPage::settingsChanged → MainWindow::onSettingsChanged → createProvider()」，
    // 这意味着任何字段（包括 userName / assistantName 这种纯 UI 字段）的修改都会
    // 触发 Provider 重建——如果正在流式回复就会被中断。
    //
    // 新路径：AppSettings 自己根据字段类型分两路 emit，并做 200ms 防抖。
    //   providerSettingsChanged → onProviderSettingsChanged → createProvider()
    //   uiSettingsChanged       → onUiSettingsChanged       → 仅刷新角色名 / 立绘资源
    // SettingPage::settingsChanged 暂时仍会发射但已无监听者，下个 commit 会清掉它们。
    connect(m_settings, &AppSettings::providerSettingsChanged,
            this, &MainWindow::onProviderSettingsChanged);
    connect(m_settings, &AppSettings::uiSettingsChanged,
            this, &MainWindow::onUiSettingsChanged);

    // === SessionManager 状态变化 → 自动更新 ChatPage ===
    connect(m_sessionManager, &SessionManager::sessionCreated,
            this, [this](ChatSession *session) {
        m_chatPage->addSession(session);           // 新会话加入列表
    });
    connect(m_sessionManager, &SessionManager::sessionDeleted,
            this, [this](const QString &id) {
        m_chatPage->removeSession(id);             // 从列表移除
    });
    connect(m_sessionManager, &SessionManager::activeSessionChanged,
            this, [this](ChatSession *session) {
        if (session) {
            m_chatPage->loadMessages(session->messages());  // 加载消息记录
            m_chatPage->setActiveSession(session->id());    // 高亮选中项
        } else {
            m_chatPage->clearMessages();
        }
    });

    // === 快捷键：Ctrl+T 切换立绘窗口显示/隐藏 ===
    auto *tachieShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
    connect(tachieShortcut, &QShortcut::activated, this, [this]() {
        if (m_tachieWindow) {
            m_tachieWindow->setVisible(!m_tachieWindow->isVisible());
        }
    });

    // === 快捷键：Esc 中止当前流式回复并保留已收到的 partial reply ===
    // 流式途中按 Esc：把 LLMProvider::accumulatedResponse() 的内容当作"完成"
    // 流程保存到 session（自然 finished 路径会做的事），状态文字改 "Aborted"。
    auto *abortShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(abortShortcut, &QShortcut::activated, this,
            &MainWindow::abortStreamingAndSavePartial);
}

// ============================================================================
// Provider 管理
// ============================================================================

/**
 * @brief 根据当前 Provider 选择创建/重建 LLMProvider 实例
 *
 * 流程：
 * 1. 中止并销毁旧的 Provider
 * 2. 读取 ChatPage 中 ComboBox 的当前选择（"claude" 或 "openai"）
 * 3. 从 AppSettings 读取对应的 API Key / Base URL / Model
 * 4. 创建 ClaudeProvider 或 OpenAIProvider
 * 5. 连接 tokenReceived / responseFinished / errorOccurred 信号
 */
void MainWindow::createProvider()
{
    // 清理旧 Provider
    if (m_provider) {
        m_provider->abort();
        m_provider->deleteLater();
        m_provider = nullptr;
    }
    // 切换 Provider 时丢掉未 flush 的 token（属于已废弃的旧请求）
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    m_pendingBubbleText.clear();
    m_reasoningBuffer.clear();
    m_firstTokenSeen = false;

    // 读取当前选择并持久化
    QString providerName = m_chatPage->currentProviderData();
    m_settings->setActiveProvider(providerName);

    // 根据选择创建对应的 Provider
    if (providerName == "claude") {
        m_provider = new ClaudeProvider(
            m_nam,
            m_settings->claudeApiKey(),
            m_settings->claudeBaseUrl(),
            m_settings->claudeModel(),
            m_settings->claudeReasoningEffort(),
            this
        );
    } else if (providerName == "gemini") {
        m_provider = new GeminiProvider(
            m_nam,
            m_settings->geminiApiKey(),
            m_settings->geminiBaseUrl(),
            m_settings->geminiModel(),
            this
        );
    } else {
        m_provider = new OpenAIProvider(
            m_nam,
            m_settings->openaiApiKey(),
            m_settings->openaiBaseUrl(),
            m_settings->openaiModel(),
            m_settings->openaiReasoningEffort(),
            this
        );
    }

    // 连接 Provider 的三个核心信号
    connect(m_provider, &LLMProvider::tokenReceived, this, &MainWindow::onTokenReceived);
    connect(m_provider, &LLMProvider::responseFinished, this, &MainWindow::onResponseFinished);
    connect(m_provider, &LLMProvider::errorOccurred, this, &MainWindow::onProviderError);
    connect(m_provider, &LLMProvider::reasoningTokenReceived,
            this, &MainWindow::onReasoningTokenReceived);
    connect(m_provider, &LLMProvider::imageGenerationStarted,
            this, &MainWindow::onImageGenerationStarted);
}

/**
 * @brief 从磁盘加载所有会话并刷新 ChatPage 的会话列表
 */
void MainWindow::loadSessions()
{
    m_sessionManager->loadAllSessions();
    QList<ChatSession*> sessions = m_sessionManager->allSessions();
    m_chatPage->refreshSessionList(sessions);

    // 恢复上次活跃的会话
    if (m_sessionManager->activeSession()) {
        ChatSession *active = m_sessionManager->activeSession();
        m_chatPage->loadMessages(active->messages());
        m_chatPage->setActiveSession(active->id());
    }
}

// ============================================================================
// 立绘窗口初始化
// ============================================================================

/**
 * @brief 创建并初始化立绘窗口
 *
 * 从 AppSettings 读取是否启用立绘，若启用则：
 * 1. 构造 TachieWindow，资源目录为 <exe_dir>/resources/ATRI0.3/
 * 2. 恢复上次关闭时的窗口位置（首次启动则使用默认位置）
 * 3. 连接位置变化信号以便记忆窗口位置
 * 4. 显示立绘窗口
 */
void MainWindow::setupTachie()
{
    m_tachieEnabled = m_settings->tachieEnabled();
    if (!m_tachieEnabled) return;

    QString resDir = QCoreApplication::applicationDirPath() + "/config/" + m_settings->tachieResourceDir();
    m_tachieWindow = new TachieWindow(resDir);

    // 恢复记忆的窗口位置
    int posX = m_settings->tachiePositionX();
    int posY = m_settings->tachiePositionY();
    if (posX >= 0 && posY >= 0) {
        m_tachieWindow->move(posX, posY);
    }

    // 当用户拖拽立绘窗口后，记忆新位置
    connect(m_tachieWindow, &TachieWindow::positionChanged,
            this, [this](int x, int y) {
        m_settings->setTachiePositionX(x);
        m_settings->setTachiePositionY(y);
    });

    m_tachieWindow->show();
}

/**
 * @brief 构建包含情绪标签指令的 system prompt
 *
 * 在用户设置的 system prompt 末尾追加情绪标签指令，
 * 告知 AI 每次回复必须以 [情绪名] 开头，并列出所有可选情绪。
 * 可选情绪列表从立绘资源目录中的 PNG 文件名动态生成。
 *
 * @return 拼接后的完整 system prompt
 */
QString MainWindow::buildAugmentedSystemPrompt() const
{
    QString base = m_settings->systemPrompt();

    // 若立绘未启用或窗口不存在，直接返回原始 prompt
    if (!m_tachieEnabled || !m_tachieWindow) {
        return base;
    }

    QStringList emotions = m_tachieWindow->availableEmotions();
    if (emotions.isEmpty()) {
        return base;
    }

    QString emotionList = emotions.join(", ");
    QString instruction = QStringLiteral(
        "\n\n[OUTPUT FORMAT REQUIREMENT]\n"
        "A companion avatar app is connected to this chat. "
        "To control the avatar's expression, you MUST prepend a mood tag at the very beginning of every response.\n"
        "Format: [tag]your response text...\n"
        "Available tags: %1\n"
        "Examples:\n"
        "[高兴]你好呀！今天天气真好。\n"
        "[正常]好的，我来帮你分析一下这个问题。\n"
        "[疑惑]你说的是哪个部分呢？\n"
        "Choose the tag that best matches the tone of your response. Default to [正常] if uncertain.\n"
        "This is a strict output format rule, not a request to express emotions. NEVER omit the tag."
    ).arg(emotionList);

    return base + instruction;
}

// ============================================================================
// 槽函数 —— 会话操作
// ============================================================================

/** @brief 新建聊天会话（流式请求中禁止操作） */
void MainWindow::onNewChat()
{
    if (m_isStreaming) return;
    ChatSession *session = m_sessionManager->createSession();
    session->setProviderName(m_chatPage->currentProviderData());
    const QString providerName = m_chatPage->currentProviderData();
    session->setModelName(providerName == "claude"
                          ? m_settings->claudeModel()
                          : providerName == "gemini"
                                ? m_settings->geminiModel()
                                : m_settings->openaiModel());
    m_chatPage->clearMessages();
}

/** @brief 删除指定会话，删除后自动切换到其他活跃会话 */
void MainWindow::onDeleteChat(const QString &sessionId)
{
    if (m_isStreaming) return;

    // 删除前再确认：sessions/{uuid}.json 会被清掉，且当前会话内的所有消息都会丢失。
    ChatSession *target = m_sessionManager->session(sessionId);
    const QString title = target ? target->title() : QStringLiteral("this session");

    ElaContentDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Delete Session"));
    // 用 QWidget + QVBoxLayout 包一下：直接 setCentralWidget(QLabel) 的话
    // ElaContentDialog 内部会按 sizeHint 给一个非常贴边的尺寸，看起来很挤。
    QWidget *content = new QWidget(&dlg);
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 12);
    layout->setSpacing(8);
    QLabel *body = new QLabel(content);
    body->setText(QStringLiteral("Delete \"%1\"?").arg(title));
    QFont titleFont = body->font();
    titleFont.setPointSize(qMax(titleFont.pointSize(), 11));
    body->setFont(titleFont);
    body->setWordWrap(true);
    QLabel *hint = new QLabel(content);
    hint->setText(QStringLiteral("This will permanently remove the session and its messages."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: rgba(128, 128, 128, 0.85);"));
    layout->addWidget(body);
    layout->addWidget(hint);
    // 显式撑开尺寸：ElaContentDialog 按 centralWidget 的 sizeHint 决定窗口大小，
    // 但带 wordWrap 的 QLabel 在未确定宽度前 sizeHint().height() 偏小，
    // 结果 dialog 会比内容矮，hint 行被底部按钮条遮住。固定 minSize 兜底。
    {
        QFontMetrics bodyFm(titleFont);
        QFontMetrics hintFm(hint->font());
        const int minH = 24 + bodyFm.height() * 2 + 8 + hintFm.height() * 2 + 12;
        content->setMinimumSize(420, minH);
    }
    dlg.setCentralWidget(content);
    dlg.setLeftButtonText(QStringLiteral("Cancel"));
    dlg.setRightButtonText(QStringLiteral("Delete"));

    bool confirmed = false;
    QObject::connect(&dlg, &ElaContentDialog::rightButtonClicked, [&]() { confirmed = true; });
    dlg.exec();
    if (!confirmed) return;

    m_sessionManager->deleteSession(sessionId);

    ChatSession *active = m_sessionManager->activeSession();
    if (active) {
        m_chatPage->loadMessages(active->messages());
    } else {
        m_chatPage->clearMessages();
    }
}

/** @brief 重命名指定会话，更新标题并持久化（rename 文本输入由 SessionListWidget 完成） */
void MainWindow::onRenameChat(const QString &sessionId, const QString &newName)
{
    if (m_isStreaming) return;

    ChatSession *session = m_sessionManager->session(sessionId);
    if (!session) return;

    const QString title = newName.trimmed();
    if (title.isEmpty() || title == session->title()) return;

    session->setTitle(title);
    m_sessionManager->saveSession(session);
}

void MainWindow::onExportSession(const QString &sessionId)
{
    ChatSession *session = m_sessionManager->session(sessionId);
    if (!session) return;

    QString fileName = session->title().trimmed();
    if (fileName.isEmpty()) {
        fileName = "session";
    }
    // 文件名非法字符替换为下划线；正则为静态常量，避免每次导出重新编译
    static const QRegularExpression kInvalidFileNameChars(
        QStringLiteral("[\\\\/:*?\"<>|]"));
    fileName.replace(kInvalidFileNameChars, "_");

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "Export Session",
        fileName + ".md",
        "Markdown (*.md);;Text (*.txt);;All Files (*)"
    );
    if (filePath.isEmpty()) {
        return;
    }

    QString markdown;
    markdown += "# " + session->title() + "\n\n";
    if (!session->providerName().isEmpty() || !session->modelName().isEmpty()) {
        markdown += QStringLiteral("- Provider: %1\n- Model: %2\n\n")
            .arg(session->providerName(), session->modelName());
    }

    for (const auto &msg : session->messages()) {
        const QString role = (msg.role == "assistant") ? "Assistant" : "User";
        markdown += "## " + role + "\n\n";

        if (!msg.attachments.isEmpty()) {
            markdown += "Attachments:\n";
            for (const auto &att : msg.attachments) {
                QString type = "File";
                if (att.type == Attachment::Image) {
                    type = "Image";
                } else if (att.type == Attachment::Document) {
                    type = "Document";
                } else if (att.type == Attachment::TextFile) {
                    type = "Text";
                }

                markdown += QStringLiteral("- %1: %2")
                    .arg(type, att.fileName);
                if (!att.mimeType.isEmpty()) {
                    markdown += QStringLiteral(" (%1)").arg(att.mimeType);
                }
                markdown += "\n";
            }
            markdown += "\n";
        }

        markdown += msg.content.trimmed();
        markdown += "\n\n---\n\n";
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    file.write(markdown.toUtf8());
}

/** @brief 用户在会话列表中点击切换会话 */
void MainWindow::onSessionSelected(const QString &sessionId)
{
    if (m_isStreaming) return;
    m_sessionManager->setActiveSession(sessionId);
}

// ============================================================================
// 槽函数 —— 消息发送与流式接收（核心流程）
// ============================================================================

/**
 * @brief 用户发送消息的完整流程
 *
 * 1. 若无活跃会话，自动创建一个
 * 2. 将用户消息加入 ChatSession 并显示气泡
 * 3. 预先创建一个空的 assistant 气泡（用于后续流式填充）
 * 4. 禁用输入框，显示 "Thinking..." 状态
 * 5. 调用 LLMProvider 发起流式请求
 */
void MainWindow::onSendMessage(const QString &text, const QList<Attachment> &attachments)
{
    if (m_isStreaming) return;

    // 自动创建会话
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) {
        session = m_sessionManager->createSession();
        session->setProviderName(m_chatPage->currentProviderData());
        const QString providerName = m_chatPage->currentProviderData();
        session->setModelName(providerName == "claude"
                              ? m_settings->claudeModel()
                              : providerName == "gemini"
                                    ? m_settings->geminiModel()
                                    : m_settings->openaiModel());
    }

    // 记录用户消息
    ChatMessage userMsg;
    userMsg.role = "user";
    userMsg.content = text;
    userMsg.attachments = attachments;
    session->addMessage(userMsg);
    m_chatPage->addMessageBubble("user", text, attachments);

    // 预建空 assistant 气泡 + 重置流式状态 + 发起请求（与 onMessageRegenerate 共用）
    beginStreamingForActiveSession();
}

/**
 * @brief 收到流式 token 的处理逻辑
 *
 * 若立绘启用且尚未解析情绪标签，前几个 token 会被缓冲到 m_tokenBuffer 中，
 * 等待检测 [情绪名] 格式的标签。检测到后触发立绘表情切换，剩余文本转发给气泡。
 * 若缓冲超过 50 字符仍未找到 ']'，则判定无标签，将整个缓冲转发。
 */
void MainWindow::onTokenReceived(const QString &token)
{
    // 第一条正文 token 到达时把状态文字从 "Thinking..." 切成 "Generating..."；
    // 让用户能区分"模型在憋着"和"模型在出文"。reasoning 不计入这里。
    if (!m_firstTokenSeen) {
        m_firstTokenSeen = true;
        m_chatPage->setStatusText("Generating...");
    }

    // 立绘关闭或标签已解析 → 直接转发给 ChatPage
    if (!m_tachieEnabled || !m_tachieWindow || m_emotionTagParsed) {
        queuePendingBubbleText(token);
        return;
    }

    // 缓冲 token 用于提取情绪标签
    m_tokenBuffer += token;

    // 检查缓冲中是否包含 ']'（标签结束符）
    int closeBracket = m_tokenBuffer.indexOf(']');
    if (closeBracket >= 0) {
        m_emotionTagParsed = true;

        // 用正则提取 [情绪名]；正则为静态常量，避免每个 token 都重新编译
        static const QRegularExpression reEmotionTag(
            QStringLiteral("^\\s*\\[([^\\]]+)\\]"));
        QRegularExpressionMatch match = reEmotionTag.match(m_tokenBuffer);

        if (match.hasMatch()) {
            QString emotion = match.captured(1);
            m_tachieWindow->changeExpression(emotion);

            // 标签之后的文本转发给气泡
            QString remaining = m_tokenBuffer.mid(match.capturedEnd());
            if (!remaining.isEmpty()) {
                queuePendingBubbleText(remaining);
            }
        } else {
            // 有 ']' 但不匹配标签格式 → 整个缓冲作为普通文本转发
            queuePendingBubbleText(m_tokenBuffer);
        }
    } else if (m_tokenBuffer.length() > 50) {
        // 缓冲超过 50 字符仍无 ']' → 无标签，整体转发
        m_emotionTagParsed = true;
        queuePendingBubbleText(m_tokenBuffer);
    }
    // 否则继续缓冲，等待更多 token
}

/**
 * @brief 把 token 排入流式 flush 队列；80ms 后由 m_streamFlushTimer 一次性刷新
 *
 * 长回复里逐 token 调 QLabel::setText 会让 layout 跑满；累积起来分批 flush
 * 视觉上无差别但 CPU 友好得多。
 */
void MainWindow::queuePendingBubbleText(const QString &text)
{
    if (text.isEmpty()) return;
    m_pendingBubbleText += text;
    if (m_streamFlushTimer && !m_streamFlushTimer->isActive()) {
        m_streamFlushTimer->start();
    }
}

/** @brief 流式 flush 计时器到期，把累积的 token 一次性喂给气泡 */
void MainWindow::flushPendingBubbleText()
{
    if (m_pendingBubbleText.isEmpty()) return;
    m_chatPage->appendToLastBubble(m_pendingBubbleText);
    m_pendingBubbleText.clear();
}

/**
 * @brief 收到 reasoning（思考链）增量，累积到 m_reasoningBuffer
 *
 * Reasoning 不写入气泡正文（避免污染朗读 / 显示），
 * 仅在 onResponseFinished 时随 ChatMessage 一起持久化。
 */
void MainWindow::onReasoningTokenReceived(const QString &token)
{
    m_reasoningBuffer += token;
}

/**
 * @brief Provider 开始生成图像 → 切状态文字
 *
 * OpenAI /v1/responses + image / /v1/images/generations 这类请求耗时较长且
 * 没有逐 token 文本输出，沿用 "Thinking..." 会让用户以为卡死。Provider 在
 * 触发图像生成时通过 imageGenerationStarted(count) 通知；这里把状态文字切
 * 成 "Generating image..."，并把 m_firstTokenSeen 置为 true 防止后续可能的
 * 文本 token 把状态再翻回 "Generating..."。
 */
void MainWindow::onImageGenerationStarted(int count)
{
    Q_UNUSED(count);
    m_firstTokenSeen = true;
    m_chatPage->setStatusText("Generating image...");
}

/**
 * @brief 流式响应完成
 *
 * 恢复输入状态，从完整回复中剥离情绪标签 [情绪名]，
 * 用干净文本替换气泡内容，并保存到 ChatSession 持久化到磁盘。
 * 这确保聊天气泡和历史记录中不包含情绪标签。
 */
void MainWindow::onResponseFinished(const QString &fullResponse)
{
    m_isStreaming = false;
    m_chatPage->setInputEnabled(true);
    m_chatPage->setLoading(false);
    m_chatPage->setStatusText("");

    // 把 flush 队列里残留的 token 立刻喂掉，避免被随后的 replaceLastBubbleContent
    // 覆盖（仅在立绘启用时会替换内容；但停定时器是任何情况下都该做的清理）
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    flushPendingBubbleText();

    // 从完整回复中剥离开头的 [情绪名] 标签
    QString cleanResponse = fullResponse;
    if (m_tachieEnabled && m_tachieWindow) {
        // 正则为静态常量，每次响应完成只引用编译好的实例
        static const QRegularExpression reEmotionTagStrip(
            QStringLiteral("^\\s*\\[[^\\]]+\\]\\s*"));
        cleanResponse = fullResponse;
        cleanResponse.remove(reEmotionTagStrip);

        // 替换气泡内容为干净文本（去除标签）
        m_chatPage->replaceLastBubbleContent(cleanResponse);
    }

    // 保存 assistant 消息并持久化会话（保存干净文本，不含情绪标签）
    ChatSession *session = m_sessionManager->activeSession();
    if (session) {
        ChatMessage assistantMsg;
        assistantMsg.role = "assistant";
        assistantMsg.content = cleanResponse;
        assistantMsg.reasoning = m_reasoningBuffer;
        session->addMessage(assistantMsg);
        m_sessionManager->saveSession(session);
    }

    // TTS 朗读 AI 回复（预连接已就绪，直接发 SSML 零延迟）
    if (m_settings->ttsEnabled() && !cleanResponse.isEmpty()) {
        m_ttsProvider->synthesize(cleanResponse, m_settings->ttsVoice());
    }
}

/**
 * @brief Esc 中止当前流式回复并把已收到的 partial reply 保存到 session
 *
 * 流式途中按 Esc：先抓 LLMProvider::accumulatedResponse() 的内容再 abort()，
 * 然后委托 onResponseFinished 走完整的"完成"流程（情绪标签剥离 + session 持久化
 * + TTS 触发），最后把状态文字改成 "Aborted" 让用户看到是中止而非自然结束。
 * 一个 token 都没到的情况单独处理：不写空消息。
 */
void MainWindow::abortStreamingAndSavePartial()
{
    if (!m_isStreaming) return;

    const QString partial = m_provider ? m_provider->accumulatedResponse() : QString();
    if (m_provider) m_provider->abort();

    if (partial.isEmpty()) {
        // 没收到任何 token → 不往 session 写空 assistant 消息，仅恢复输入状态
        m_isStreaming = false;
        m_chatPage->setInputEnabled(true);
        m_chatPage->setLoading(false);
        m_chatPage->setStatusText("Aborted");
        if (m_streamFlushTimer) m_streamFlushTimer->stop();
        m_pendingBubbleText.clear();
        m_reasoningBuffer.clear();
        return;
    }

    // 委托给自然完成路径：onResponseFinished 会刷 pending text、剥情绪标签、
    // 保存 session、触发 TTS。完成后覆盖状态文字为 "Aborted"。
    onResponseFinished(partial);
    m_chatPage->setStatusText("Aborted");
}

/** @brief LLM 请求出错，恢复输入状态并显示错误信息 */
void MainWindow::onProviderError(const QString &error)
{
    m_isStreaming = false;
    m_chatPage->setInputEnabled(true);
    m_chatPage->setLoading(false);
    m_chatPage->setStatusText("Error: " + error);

    // 出错前已收到的 token 先落到气泡里，让用户看到 partial reply
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    flushPendingBubbleText();
}

// ============================================================================
// 槽函数 —— 设置变更
// ============================================================================

/** @brief 任一设置页参数变更后，重建 Provider 以应用新配置 */
void MainWindow::onProviderSettingsChanged()
{
    // API key / baseUrl / model / 推理强度 这类字段变了——重建 Provider，
    // 让下一次 sendStreamingRequest 走新配置。
    createProvider();
}

void MainWindow::onUiSettingsChanged()
{
    // 角色名 / 立绘资源目录这类纯 UI 字段，无需碰 Provider，
    // 否则会在用户编辑时频繁中断正在进行的流式回复。
    m_chatPage->updateRoleNames(m_settings->userName(), m_settings->assistantName());

    if (m_tachieWindow) {
        QString newDir = QCoreApplication::applicationDirPath() + "/config/" + m_settings->tachieResourceDir();
        m_tachieWindow->setResourceDir(newDir);
    }
}

/** @brief 用户在 ChatPage 切换 Provider（Claude ↔ OpenAI），重建 Provider */
void MainWindow::onProviderSwitched(const QString &providerName)
{
    Q_UNUSED(providerName)
    if (m_isStreaming) return;
    createProvider();
}

/**
 * @brief 切换单条消息的收藏状态
 *
 * 流程：
 *   1. 校验索引合法且当前有活跃会话
 *   2. 读取当前 favorite 状态并取反
 *   3. 写入 ChatSession 数据层并立即落盘
 *   4. 通知 ChatPage 刷新对应气泡的 ★ 指示器
 *
 * 不阻塞流式状态：收藏只触碰元数据，不影响进行中的请求。
 */
void MainWindow::onMessageFavoriteToggle(int index)
{
    if (index < 0) return;  // 自动分配前的占位气泡（理论上不会进入这里）
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;
    if (index >= session->messages().size()) return;

    bool newFav = !session->messageAt(index).favorite;
    session->setMessageFavorite(index, newFav);
    m_sessionManager->saveSession(session);
    m_chatPage->setBubbleFavorite(index, newFav);
}

/**
 * @brief 从指定索引开始删除消息（包含该索引），并刷新气泡列表
 *
 * 流程：
 *   1. 流式响应中拒绝操作（避免与正在写入的最后一条 assistant 消息冲突）
 *   2. 校验索引和活跃会话
 *   3. 调 ChatSession::truncateFrom 移除该索引及其后所有消息
 *   4. 立即落盘
 *   5. 用 ChatPage::loadMessages 重建气泡列表（一次清空再重新装填，
 *      索引重新连续，比逐个 removeWidget 更稳妥）
 */
void MainWindow::onMessageDeleteFromHere(int index)
{
    if (m_isStreaming) return;
    if (index < 0) return;
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;
    if (index >= session->messages().size()) return;

    session->truncateFrom(index);
    m_sessionManager->saveSession(session);
    m_chatPage->loadMessages(session->messages());
}

/**
 * @brief 启动一轮流式请求的公共序章
 *
 * 假设 session 已由调用方准备好（onSendMessage 加完 user 消息 / onMessageRegenerate
 * 截断到 user 消息），这里负责：预建空 assistant 气泡、把 UI 切到流式状态、
 * 重置缓冲与 first-token / TTS 状态、发起 sendStreamingRequest。
 *
 * 没有活跃 session 时直接 no-op，调用方先校验。
 */
void MainWindow::beginStreamingForActiveSession()
{
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;

    m_chatPage->addMessageBubble("assistant", "");

    m_isStreaming = true;
    m_chatPage->setInputEnabled(false);
    m_chatPage->setStatusText("Thinking...");
    m_chatPage->setLoading(true);

    m_emotionTagParsed = false;
    m_tokenBuffer.clear();
    m_pendingBubbleText.clear();
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    m_reasoningBuffer.clear();
    m_firstTokenSeen = false;

    m_ttsProvider->abort();
    m_ttsPendingPlay = false;
    m_ttsPlayer->stop();
    m_ttsPlayer->setMedia(QMediaContent());
    if (m_settings->ttsEnabled()) {
        m_ttsProvider->preConnect();
    }

    m_provider->sendStreamingRequest(
        session->messages(),
        buildAugmentedSystemPrompt(),
        m_settings->maxTokens(),
        m_settings->temperature()
    );
}

/**
 * @brief 重新生成指定 assistant 回复
 *
 * 在 assistant 气泡上点"Regenerate from here"：把该消息及其后的所有消息从
 * session 截断（保留前一条 user 消息为最后输入），然后走完整的流式发起流程
 * 重新喂给 Provider。流式中或前一条不是 user 消息时直接 no-op。
 */
void MainWindow::onMessageRegenerate(int index)
{
    if (m_isStreaming) return;
    if (index <= 0) return;     // 至少要有前置 user 消息才有重发的意义
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;
    if (index >= session->messages().size()) return;
    if (session->messageAt(index).role != "assistant") return;
    if (session->messageAt(index - 1).role != "user") return;

    // 截断 + 持久化 + 重新渲染气泡（少 1 条 assistant，最末是触发重发的 user）
    session->truncateFrom(index);
    m_sessionManager->saveSession(session);
    m_chatPage->loadMessages(session->messages());

    // 预建空 assistant 气泡 + 重置流式状态 + 发起请求（与 onSendMessage 共用）
    beginStreamingForActiveSession();
}
