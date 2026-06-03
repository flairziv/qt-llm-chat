#include "MainWindow.h"
#include "ChatPage.h"
#include "AnalyticsPage.h"
#include "AsciiArtPage.h"
#include "SettingClaudePage.h"
#include "SettingGeminiPage.h"
#include "SettingOpenAIPage.h"
#include "SettingGeneralPage.h"
#include "SettingToolsPage.h"
#include "TachieWindow.h"
#include "core/AppSettings.h"
#include "core/SessionManager.h"
#include "core/ClaudeProvider.h"
#include "core/GeminiProvider.h"
#include "core/OpenAIProvider.h"
#include "core/ChatSession.h"
#include "core/EdgeTTSProvider.h"
#include "core/LLMProvider.h"
#include "core/ToolRegistry.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShortcut>
#include <QMediaPlayer>
#include <QDebug>
#include <QTimer>
#include <QVBoxLayout>
#include <QRandomGenerator>
#include <QtConcurrent>

#include "ElaContentDialog.h"

namespace {

/**
 * @brief 从 assistant 回复里抽取 [Generated Image Saved] 标记 → Attachment 列表
 *
 * OpenAI image / gpt-image 模式落地的图片文件由 provider 写盘后以
 *   `[Generated Image Saved] <绝对路径>`
 * 形式插在回复正文里。这里逐行匹配、读出文件字节做成 Attachment::Image，
 * 然后把所有标记从 responseText 里删掉，再合并多余空行 + 首尾 trim，
 * 让最终落盘到 ChatMessage 的正文只剩"模型说的话"。读不开的文件直接跳过
 * （避免一张图丢了整段回复也丢）。
 */
QList<Attachment> extractGeneratedImageAttachments(QString *responseText)
{
    QList<Attachment> attachments;
    if (!responseText) {
        return attachments;
    }

    const QRegularExpression savedImageRe(
        QStringLiteral("^\\s*\\[Generated Image Saved\\]\\s+(.+?)\\s*$"),
        QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = savedImageRe.globalMatch(*responseText);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString imagePath = QDir::fromNativeSeparators(match.captured(1).trimmed());
        QFile file(imagePath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }

        Attachment att;
        att.type = Attachment::Image;
        att.fileName = QFileInfo(imagePath).fileName();
        att.mimeType = QStringLiteral("image/png");
        att.fileData = file.readAll();
        if (!att.fileData.isEmpty()) {
            attachments.append(att);
        }
    }

    responseText->remove(savedImageRe);
    responseText->replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    *responseText = responseText->trimmed();
    return attachments;
}

} // namespace

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

    // QMediaPlayer 播放失败诊断（missing codec / wmfengine.dll / 设备占用等），
    // 没有这条日志时 stop / setMedia 链路上的回放失败完全静默 —— 用户只看到
    // 立绘嘴不动、声音不响，难以定位。
    connect(m_ttsPlayer,
            QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
            this, [this](QMediaPlayer::Error err) {
        qWarning() << "[TTS] QMediaPlayer error:" << err
                   << "errorString=" << m_ttsPlayer->errorString();
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
    setWindowIcon(QIcon(":/imgs/combined.ico"));
    setUserInfoCardPixmap(QIcon(":/imgs/favicon.ico").pixmap(64, 64));
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

    m_toolsPage = new SettingToolsPage(m_settings, this);
    addPageNode("Tools", m_toolsPage, settingsGroup, ElaIconType::ScrewdriverWrench);
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
    connect(m_chatPage, &ChatPage::messageEditRequested,
            this, &MainWindow::onMessageEdit);

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
            m_settings,
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
            m_settings->openaiImageApiMode(),
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

    // 新的用户回合：复位 agentic 工具循环计数（工具续传不复位，只有用户发起才清零）
    m_agenticIterations = 0;

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

    // [Generated Image Saved/URL] 标记在 onResponseFinished 里才转成 Attachment，
    // 流式期间先把它们从显示文本里剥掉，否则用户会先看到一行裸路径再被替换，
    // 视觉跳变很明显。这里只动 display；m_pendingBubbleText 才进气泡。
    QString display = text;
    static const QRegularExpression imgMarkerRe(
        QStringLiteral("\\n?\\[Generated Image (?:Saved|URL)\\][^\\n]*\\n?"));
    display.remove(imgMarkerRe);
    if (display.isEmpty()) {
        return;
    }

    m_pendingBubbleText += display;
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
 * @brief 收到 reasoning（思考链）增量
 *
 * 两件事：
 *   1. 累积到 m_reasoningBuffer，onResponseFinished 时随 ChatMessage 一起持久化
 *   2. 实时推到最后一个 assistant 气泡的折叠区块里，让用户可以一边看 thinking
 *      展开一边等正文（默认折叠，需点击展开）
 *
 * 不写入气泡**正文** —— 避免污染朗读 / token-flush 路径。
 */
void MainWindow::onReasoningTokenReceived(const QString &token)
{
    m_reasoningBuffer += token;
    m_chatPage->appendReasoningToLastBubble(token);
}

/**
 * @brief Provider 开始生成图像 → 切状态文字 + 插入占位符
 *
 * OpenAI /v1/responses + image / /v1/images/generations 这类请求耗时较长且
 * 没有逐 token 文本输出，沿用 "Thinking..." 会让用户以为卡死。Provider 在
 * 触发图像生成时通过 imageGenerationStarted(count) 通知；这里：
 *   1. 状态文字切成 "Generating image..."
 *   2. m_firstTokenSeen 置 true，防止后续可能的文本 token 把状态翻回 "Generating..."
 *   3. 给当前 assistant 气泡塞 count 个 ShimmerWidget 占位符，让用户看到"正在画"
 *      的视觉反馈；onResponseFinished / onProviderError 会清掉它们
 */
void MainWindow::onImageGenerationStarted(int count)
{
    m_firstTokenSeen = true;
    m_chatPage->setStatusText("Generating image...");
    m_chatPage->addImagePlaceholderToLastBubble(qMax(1, count));
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
    // 图片生成期间挂在气泡上的 ShimmerWidget 占位符必须收掉，否则会一直停留
    // 在 "正在画" 状态。即便本轮没启用图像生成，clearImagePlaceholdersInLastBubble
    // 在空列表上是 no-op，调用代价可忽略。
    m_chatPage->clearImagePlaceholdersInLastBubble();

    // 把 flush 队列里残留的 token 立刻喂掉，避免被随后的 replaceLastBubbleContent
    // 覆盖（仅在立绘启用时会替换内容；但停定时器是任何情况下都该做的清理）
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    flushPendingBubbleText();

    // 从完整回复中剥离开头的 [情绪名] 标签
    QString cleanResponse = fullResponse;

    // 1. 先抽 [Generated Image Saved] 标记 → Attachment 列表（同时从 cleanResponse 删掉那些行）
    //    必须先于情绪标签剥离，否则 "[Generated Image Saved] ..." 的方括号可能被
    //    情绪正则误吃首段。
    QList<Attachment> generatedAttachments = extractGeneratedImageAttachments(&cleanResponse);

    // 2. 立绘开启时再剥首段情绪标签
    if (m_tachieEnabled && m_tachieWindow) {
        // 正则为静态常量，每次响应完成只引用编译好的实例
        static const QRegularExpression reEmotionTagStrip(
            QStringLiteral("^\\s*\\[[^\\]]+\\]\\s*"));
        cleanResponse.remove(reEmotionTagStrip);
    }

    // 3. 同步气泡：有生成图片用 replaceLastBubbleMessage 同时换正文 + 附件；
    //    仅文本（去标签后）用 replaceLastBubbleContent。两条路径都让占位符腾位置给真图。
    if (!generatedAttachments.isEmpty()) {
        m_chatPage->replaceLastBubbleMessage(cleanResponse, generatedAttachments);
    } else if (m_tachieEnabled && m_tachieWindow) {
        m_chatPage->replaceLastBubbleContent(cleanResponse);
    }

    // 本轮模型是否发起了工具调用。abort()（Esc / 设置变更）会清空 pendingToolCalls，
    // 所以中止路径下这里恒为空 —— assistant 消息按纯文本保存，不会落下一个没有
    // 对应 tool_result 的悬空 tool_use（那会让下一次请求被 Claude 400）。
    const QList<ToolCall> toolCalls =
        m_provider ? m_provider->pendingToolCalls() : QList<ToolCall>();
    const bool runTools = !toolCalls.isEmpty();

    // 保存 assistant 消息并持久化会话（保存干净文本，不含情绪标签）。
    // 仅在确实要执行工具时把 toolCalls 一并写入 —— 保证 tool_use 与随后追加的
    // tool_result 成对出现，重放 / 续传时请求体合法。
    ChatSession *session = m_sessionManager->activeSession();
    if (session) {
        ChatMessage assistantMsg;
        assistantMsg.role = "assistant";
        assistantMsg.content = cleanResponse;
        assistantMsg.reasoning = m_reasoningBuffer;
        assistantMsg.attachments = generatedAttachments;
        if (runTools) assistantMsg.toolCalls = toolCalls;
        session->addMessage(assistantMsg);
        m_sessionManager->saveSession(session);

        // 当前会话刚有新回复 → 推到列表顶部，最近活跃优先
        m_chatPage->moveSessionToTop(session->id());
    }

    // 工具路径：执行工具 + 回传结果 + 继续 agentic 循环，跳过下面的「本轮结束」收尾
    //（不恢复输入、不报 token 数、不生成标题、不朗读 —— 这些都留给最终回合）。
    if (runTools && session) {
        runToolCallsAndContinue(toolCalls);
        return;
    }

    // ---- 最终回合结束：恢复输入状态 + 状态栏耗时 + 自动标题 + TTS ----
    m_isStreaming = false;
    m_chatPage->setInputEnabled(true);
    m_chatPage->setLoading(false);

    // 计算耗时 + 粗估 token 数（≈ 4 字符/token），状态栏显示 3 秒后自动清。
    // singleShot 里检查 m_isStreaming：3 秒内用户又发起一轮就让那轮的状态文本接管。
    const qint64 elapsedMs = m_requestStartTime.msecsTo(QDateTime::currentDateTime());
    const double elapsedSec = elapsedMs / 1000.0;
    const int estimatedTokens = fullResponse.length() / 4;
    m_chatPage->setStatusText(
        QStringLiteral("%1 tokens | %2s").arg(estimatedTokens).arg(elapsedSec, 0, 'f', 1));
    QTimer::singleShot(3000, this, [this]() {
        if (!m_isStreaming) m_chatPage->setStatusText("");
    });

    // 首轮对话完成（user + assistant 共 2 条）→ 自动生成会话标题
    if (session && session->messages().size() == 2) {
        generateSessionTitle(session);
    }

    // TTS 朗读 AI 回复（预连接已就绪，直接发 SSML 零延迟）
    if (m_settings->ttsEnabled() && !cleanResponse.isEmpty()) {
        m_ttsProvider->synthesize(cleanResponse, m_settings->ttsVoice());
    }
}

/**
 * @brief 执行模型发起的工具调用（异步），结果由 onToolExecFinished 回传并续传
 *
 * onResponseFinished 发现本轮有未决 tool_use 时调用。拆成跨线程的两半：
 *   1. 本函数（GUI 线程）：逐个过审批门（C6，模态对话框必须在 GUI 线程）。通过的
 *      调用收进 work 列表（只带跨线程安全的纯字符串），被拒的当场生成 isError 结果
 *      存入 m_toolExecDenied。随后把 work 丢进 QtConcurrent 线程池执行——fetch_url
 *      是同步 GET（最长 15s），搬到线程池后主线程不再冻结。置 m_toolExecInProgress。
 *   2. onToolExecFinished（执行完回到 GUI 线程）：按原始顺序重组 tool_result，拼成
 *      一条 user 消息落盘 + 合并渲染，再做失控保护 / 续传下一轮。
 *
 * 线程安全：fetch_url 用本地 QNAM + 本地事件循环，自包含、线程内自洽，可安全在
 * 工作线程跑；ToolRegistry 启动后只读，跨线程并发读无数据竞争。execute 不抛
 * （异常在 ToolRegistry 内兜底成 isError），故工作线程 lambda 无需 try/catch。
 */
void MainWindow::runToolCallsAndContinue(const QList<ToolCall> &calls)
{
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;

    // 审批必须在 GUI 线程跑完：通过的进 work（纯数据，跨线程安全），被拒的当场生成
    // isError 结果存 m_toolExecDenied，finished 时按 id 取回。既保持 tool_use/
    // tool_result 成对（会话合法），也让模型知道"被拒绝"而不是干等。
    struct ApprovedCall { QString id; QString name; QString args; };
    QList<ApprovedCall> work;
    m_toolExecCalls = calls;
    m_toolExecDenied.clear();
    m_toolExecCancelled = false;   // 新一批执行：清掉上一批可能残留的取消标志
    for (const ToolCall &call : calls) {
        if (approveToolCall(call)) {
            work.append({ call.id, call.name, call.argsJson });
        } else {
            ToolResult denied;
            denied.isError = true;
            denied.content = QStringLiteral("Tool call denied by the user.");
            denied.toolUseId = call.id;
            m_toolExecDenied.insert(call.id, denied);
        }
    }

    // 进入"工具执行中"：输入仍禁用、转圈继续（沿用本轮流式的 busy 态），状态切到
    // "Running tools..."。m_isStreaming 保持 true，但 Esc 路径会据 m_toolExecInProgress
    // 区分出"没有活跃网络流可中止"。
    m_toolExecInProgress = true;
    m_chatPage->setStatusText(QStringLiteral("Running tools..."));

    if (!m_toolWatcher) {
        m_toolWatcher = new QFutureWatcher<QList<ToolResult>>(this);
        connect(m_toolWatcher, &QFutureWatcher<QList<ToolResult>>::finished,
                this, &MainWindow::onToolExecFinished);
    }

    // 工作线程顺序执行已审批工具，每个结果回填对应 tool_use 的 id。
    m_toolWatcher->setFuture(QtConcurrent::run([work]() -> QList<ToolResult> {
        QList<ToolResult> out;
        out.reserve(work.size());
        for (const ApprovedCall &c : work) {
            ToolResult r = ToolRegistry::instance().execute(c.name, c.args);
            r.toolUseId = c.id;
            out.append(r);
        }
        return out;
    }));
}

/**
 * @brief 工具在工作线程执行完毕，回到 GUI 线程：重组结果 → 落盘 → 续传 / 收尾
 *
 * 按 m_toolExecCalls 的原始顺序重组每个 tool_result（被拒的取 m_toolExecDenied，
 * 已执行的取 future 结果），保证 tool_use 与 tool_result 顺序、配对一致。随后追加
 * 一条携带 tool_result 的 user 消息、持久化、合并渲染到发起调用的 assistant 气泡，
 * 最后做失控保护：超上限即停（结果仍成对落盘，只是不再回传），否则发起下一轮。
 */
void MainWindow::onToolExecFinished()
{
    m_toolExecInProgress = false;

    // 已被 Esc 逻辑取消：abort 路径已写过 tool_result 并复位 UI，这里只丢弃工作线程
    // 迟到的结果（QtConcurrent::run 无法中断，工具已在后台跑完），不再重复落盘。
    if (m_toolExecCancelled) {
        m_toolExecCancelled = false;
        return;
    }

    // 执行结果按 toolUseId 建索引，叠加到"被拒"映射上，便于按原始顺序取回。
    QHash<QString, ToolResult> byId = m_toolExecDenied;
    const QList<ToolResult> execResults = m_toolWatcher->result();
    for (const ToolResult &r : execResults) {
        byId.insert(r.toolUseId, r);
    }

    ChatSession *session = m_sessionManager->activeSession();
    if (!session) {
        // 会话在执行期间没了（m_isStreaming 守卫本应挡住切换，这里兜底）：回到空闲态。
        m_isStreaming = false;
        m_chatPage->setInputEnabled(true);
        m_chatPage->setLoading(false);
        return;
    }

    // 拼 tool_result 消息落盘 + 合并渲染。byId 覆盖全部调用（被拒 + 已执行），
    // 故 fallback 不会触发；仍传一个防御性文案以防意外缺项。
    appendToolResultsMessage(byId, QStringLiteral("Tool produced no result."));

    // 失控保护：超过上限就停在这 —— 工具已执行、结果已落盘（tool_use/tool_result
    // 成对，会话合法），只是不再自动把结果喂回模型，用户可手动续。
    if (++m_agenticIterations > kMaxAgenticIterations) {
        m_isStreaming = false;
        m_chatPage->setInputEnabled(true);
        m_chatPage->setLoading(false);
        m_chatPage->setStatusText("Tool loop limit reached");
        return;
    }

    // 把工具结果作为新一轮历史回传：预建新 assistant 气泡 + 重置流式状态 + 发请求
    beginStreamingForActiveSession();
}

/**
 * @brief 按 m_toolExecCalls 原始顺序拼一条 user(tool_result) 消息，落盘 + 合并渲染
 *
 * resultsById 提供每个 tool_use 对应结果（按 toolUseId）；未命中的调用兜底成一条
 * isError 结果、content 取 fallbackContent。两个调用方共用：
 *   - onToolExecFinished：resultsById = 被拒 + 工作线程执行结果（覆盖全部，fallback 不触发）
 *   - abortStreamingAndSavePartial 取消路径：resultsById = 仅被拒，已审批但被取消的调用走
 *     fallback（"…cancelled…"）
 * 保证两条路径产出结构一致的 tool_result，维持 tool_use/tool_result 成对的核心不变量。
 */
void MainWindow::appendToolResultsMessage(const QHash<QString, ToolResult> &resultsById,
                                          const QString &fallbackContent)
{
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;

    ChatMessage toolMsg;
    toolMsg.role = "user";
    for (const ToolCall &call : m_toolExecCalls) {
        ToolResult r;
        if (resultsById.contains(call.id)) {
            r = resultsById.value(call.id);
        } else {
            r.isError = true;
            r.content = fallbackContent;
        }
        r.toolUseId = call.id;
        toolMsg.toolResults.append(r);
    }
    session->addMessage(toolMsg);
    m_sessionManager->saveSession(session);

    // 把本轮 tool_use + tool_result 合并显示到刚结束流式的 assistant 气泡的工具
    // 折叠区块（合成的 tool_result 消息本身不单独成气泡）。
    m_chatPage->addToolDataToLastBubble(m_toolExecCalls, toolMsg.toolResults);
}

/**
 * @brief 工具执行前的审批门（C6 审批 + C7 启用开关 / risk override）
 *
 * 顺序：未注册 → 放行（交给 ToolRegistry::execute 回 "Unknown tool"）；被设置页禁用
 * （总开关关 / 单独禁用）→ 拒绝；否则按"有效风险级别"（注册默认，被设置页 risk
 * override 覆盖）决定：ReadOnly 直接放行；Mutating / ShellOrNetwork 弹审批对话框，
 * ShellOrNetwork 选 "Allow for session" 后把工具名记入 m_sessionApprovedTools，本次
 * 运行内同名调用不再弹。
 */
bool MainWindow::approveToolCall(const ToolCall &call)
{
    const Tool *tool = ToolRegistry::instance().findTool(call.name);
    if (!tool) {
        return true;  // 未注册：放行，交给 ToolRegistry::execute 统一回 "Unknown tool"
    }

    // 工具被设置页禁用（总开关关闭或单独禁用）：直接拒绝。正常情况下禁用工具压根不会
    // 出现在 tools 数组、模型也调不到；这里兜底续传场景——禁用前会话里已有的 tool_use
    // 被回放、或执行中途总开关被关。回 false → 调用方写一条 isError 的 tool_result。
    if (!m_settings->toolsEnabled() || !m_settings->toolEnabled(call.name)) {
        return false;
    }

    // 有效风险级别：设置页的 risk override 覆盖注册默认（空 override = 用默认）。
    RiskLevel effectiveRisk = tool->riskLevel;
    const QString riskOverride = m_settings->toolRiskOverride(call.name);
    if (!riskOverride.isEmpty()) {
        effectiveRisk = riskLevelFromString(riskOverride, tool->riskLevel);
    }

    if (effectiveRisk == RiskLevel::ReadOnly) {
        return true;
    }
    if (m_sessionApprovedTools.contains(call.name)) {
        return true;
    }

    const ToolApproval decision = promptToolApproval(*tool, effectiveRisk, call.argsJson);
    if (decision == ToolApproval::AllowedForSession) {
        m_sessionApprovedTools.insert(call.name);
        return true;
    }
    return decision == ToolApproval::AllowedOnce;
}

/**
 * @brief 弹出跟随 ElaTheme 的工具审批对话框
 *
 * 展示工具名、用途说明、风险提示和入参（让用户能审 fetch_url 的 URL /
 * read_file 的路径再决定）。按钮按 effectiveRisk（注册默认 + 设置页 override）分级：
 *   - Mutating：       [Deny] ......... [Allow once]
 *   - ShellOrNetwork： [Deny] [Allow for session] [Allow once]
 * Esc / 关闭按钮不触发任何按钮信号，result 保持 Denied —— 安全默认。
 */
MainWindow::ToolApproval MainWindow::promptToolApproval(const Tool &tool, RiskLevel effectiveRisk, const QString &argsJson)
{
    ElaContentDialog dialog(this);
    dialog.setWindowTitle(tr("Tool approval"));

    QWidget *content = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 18, 20, 8);
    layout->setSpacing(8);

    QLabel *intro = new QLabel(
        tr("The assistant wants to run the tool <b>%1</b>:").arg(tool.name.toHtmlEscaped()),
        content);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);

    QLabel *descLabel = new QLabel(tool.description, content);
    descLabel->setWordWrap(true);
    descLabel->setTextFormat(Qt::PlainText);
    descLabel->setStyleSheet(QStringLiteral("color: rgba(128,128,128,0.95); font-size: 12px;"));

    const QString riskCaption = (effectiveRisk == RiskLevel::ShellOrNetwork)
        ? tr("This tool can access the network or run external commands.")
        : tr("This tool can modify files on your system.");
    QLabel *riskLabel = new QLabel(riskCaption, content);
    riskLabel->setWordWrap(true);
    riskLabel->setTextFormat(Qt::PlainText);
    riskLabel->setStyleSheet(QStringLiteral("color: #c0392b; font-size: 12px;"));

    // 入参压成单行 JSON 给用户审；空参显示占位文案
    QString argsText = argsJson.trimmed();
    if (!argsText.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(argsText.toUtf8());
        if (!doc.isNull()) {
            argsText = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        }
    }
    QLabel *argsLabel = new QLabel(argsText.isEmpty() ? tr("(no arguments)") : argsText, content);
    argsLabel->setWordWrap(true);
    argsLabel->setTextFormat(Qt::PlainText);
    argsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    argsLabel->setStyleSheet(QStringLiteral(
        "font-family: Consolas, 'Courier New', monospace; font-size: 12px; "
        "padding: 6px 10px; background: rgba(96,128,160,0.10); border-radius: 6px;"));

    layout->addWidget(intro);
    layout->addWidget(descLabel);
    layout->addWidget(riskLabel);
    layout->addWidget(argsLabel);

    dialog.setCentralWidget(content);

    // 显式 close() 保证 exec() 返回，不依赖各按钮的内置关闭行为（中间键尤其要保险）。
    ToolApproval result = ToolApproval::Denied;
    dialog.setLeftButtonText(tr("Deny"));
    QObject::connect(&dialog, &ElaContentDialog::leftButtonClicked, &dialog, [&]() {
        result = ToolApproval::Denied;
        dialog.close();
    });
    dialog.setRightButtonText(tr("Allow once"));
    QObject::connect(&dialog, &ElaContentDialog::rightButtonClicked, &dialog, [&]() {
        result = ToolApproval::AllowedOnce;
        dialog.close();
    });
    if (effectiveRisk == RiskLevel::ShellOrNetwork) {
        dialog.setMiddleButtonText(tr("Allow for session"));
        QObject::connect(&dialog, &ElaContentDialog::middleButtonClicked, &dialog, [&]() {
            result = ToolApproval::AllowedForSession;
            dialog.close();
        });
    }

    dialog.exec();
    return result;
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

    // 工具正在工作线程执行：本轮流式已结束、没有活跃网络流可中止，QtConcurrent::run
    // 也无法打断（工具会在后台跑完）。这里做"逻辑取消"：立刻为本批 tool_use 写回
    // tool_result —— 被拒的用原结果，已审批/在跑的标 cancelled —— 保持 tool_use/
    // tool_result 成对（否则用户紧接着发消息会让下次请求 400），再复位空闲。置
    // m_toolExecCancelled，迟到的 onToolExecFinished 会丢弃工作线程结果、不重复写。
    if (m_toolExecInProgress) {
        m_toolExecCancelled = true;
        m_toolExecInProgress = false;
        appendToolResultsMessage(m_toolExecDenied,
                                 QStringLiteral("Tool execution cancelled by the user."));
        m_isStreaming = false;
        m_chatPage->setInputEnabled(true);
        m_chatPage->setLoading(false);
        m_chatPage->setStatusText("Aborted");
        return;
    }

    const QString partial = m_provider ? m_provider->accumulatedResponse() : QString();
    if (m_provider) m_provider->abort();

    if (partial.isEmpty()) {
        // 没收到任何 token → 不往 session 写空 assistant 消息；同时把
        // beginStreamingForActiveSession 预建的空 assistant 气泡也清掉，
        // 避免 UI 留 orphan（session 里没这条消息，刷新会话就会消失）
        m_chatPage->removeLastBubble();
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

    // 图片生成中途出错也要把占位符收掉，否则会卡在"正在画"
    m_chatPage->clearImagePlaceholdersInLastBubble();

    // 出错前已收到的 token 先落到气泡里，让用户看到 partial reply
    if (m_streamFlushTimer) m_streamFlushTimer->stop();
    flushPendingBubbleText();

    // 与 abortStreamingAndSavePartial 一致：如果一个 token 都没到，
    // 把 beginStreamingForActiveSession 预建的空 assistant 气泡清掉，
    // 否则会留 orphan（session 没这条消息，刷新会话后会消失）。
    const QString partial = m_provider ? m_provider->accumulatedResponse() : QString();
    if (partial.isEmpty()) {
        m_chatPage->removeLastBubble();
    }

    m_chatPage->setStatusText("Error: " + error);
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
 * @brief 编辑 user 消息：把原文回填输入框，截断本条及之后的所有消息
 *
 * 这是「修改并重发」的入口：用户右键 user 气泡 → Edit Message → 原文进输入框，
 * 后续消息全被截掉。用户改完点 Send 即重新触发流式请求，等于以新内容续接对话。
 * 流式过程中禁止，避免和正在进行的请求互踩。
 */
void MainWindow::onMessageEdit(int index)
{
    if (m_isStreaming) return;
    if (index < 0) return;
    ChatSession *session = m_sessionManager->activeSession();
    if (!session) return;
    if (index >= session->messages().size()) return;

    const ChatMessage msg = session->messageAt(index);
    if (msg.role != "user") return;   // 只允许编辑 user 消息

    m_chatPage->fillInputText(msg.content);
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
    m_requestStartTime = QDateTime::currentDateTime();

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

    // 新的用户回合（重新生成）：复位 agentic 工具循环计数
    m_agenticIterations = 0;

    // 预建空 assistant 气泡 + 重置流式状态 + 发起请求（与 onSendMessage 共用）
    beginStreamingForActiveSession();
}

/**
 * @brief 用首条 user 消息让模型出一句 4-8 词的话题标题，写回会话
 *
 * 三家 provider 各走各的 endpoint（claude messages / gemini generateContent /
 * openai chat.completions），共用 m_nam。失败/超长/空返回都静默放弃，
 * 会话保留默认标题，不阻塞主流程。
 */
void MainWindow::generateSessionTitle(ChatSession *session)
{
    if (!session || session->messages().size() < 2) return;

    QString firstUserMsg = session->messages().at(0).content;
    if (firstUserMsg.isEmpty()) return;
    if (firstUserMsg.length() > 200) firstUserMsg = firstUserMsg.left(200);

    const QString providerName = session->providerName();
    QNetworkRequest request;
    QJsonObject body;
    const QString prompt = QStringLiteral(
        "Generate a short 4-8 word title summarizing this conversation topic. "
        "Reply with ONLY the title, no quotes, no punctuation at the end.\n\n"
        "User message: %1").arg(firstUserMsg);

    if (providerName == "claude") {
        if (m_settings->claudeApiKey().isEmpty()) return;
        request = LLMProvider::makeJsonRequest(QUrl(m_settings->claudeBaseUrl() + "/v1/messages"));
        request.setRawHeader("x-api-key", m_settings->claudeApiKey().toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");

        body["model"] = m_settings->claudeModel();
        body["max_tokens"] = 50;
        QJsonArray msgs;
        QJsonObject m; m["role"] = "user"; m["content"] = prompt;
        msgs.append(m);
        body["messages"] = msgs;
    } else if (providerName == "gemini") {
        if (m_settings->geminiApiKey().isEmpty()) return;
        const QString url = QStringLiteral("%1/v1beta/models/%2:generateContent?key=%3")
                                .arg(m_settings->geminiBaseUrl(),
                                     m_settings->geminiModel(),
                                     m_settings->geminiApiKey());
        request = LLMProvider::makeJsonRequest(QUrl(url));

        QJsonObject genConfig;
        genConfig["maxOutputTokens"] = 50;
        body["generationConfig"] = genConfig;
        QJsonArray contents;
        QJsonObject content;
        content["role"] = "user";
        QJsonArray parts;
        QJsonObject part;
        part["text"] = prompt;
        parts.append(part);
        content["parts"] = parts;
        contents.append(content);
        body["contents"] = contents;
    } else {
        // openai / 兼容 endpoint：本地端点（localhost / 127.0.0.1）允许空 key
        if (m_settings->openaiApiKey().isEmpty()
            && !m_settings->openaiBaseUrl().contains("localhost")
            && !m_settings->openaiBaseUrl().contains("127.0.0.1")) return;
        request = LLMProvider::makeJsonRequest(QUrl(m_settings->openaiBaseUrl() + "/v1/chat/completions"));
        if (!m_settings->openaiApiKey().isEmpty()) {
            request.setRawHeader("Authorization", ("Bearer " + m_settings->openaiApiKey()).toUtf8());
        }
        body["model"] = m_settings->openaiModel();
        body["max_tokens"] = 50;
        QJsonArray msgs;
        QJsonObject m; m["role"] = "user"; m["content"] = prompt;
        msgs.append(m);
        body["messages"] = msgs;
    }

    const QString sessionId = session->id();
    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId, providerName]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject obj = doc.object();

        QString title;
        if (providerName == "claude") {
            const QJsonArray content = obj["content"].toArray();
            if (!content.isEmpty()) title = content[0].toObject()["text"].toString();
        } else if (providerName == "gemini") {
            const QJsonArray candidates = obj["candidates"].toArray();
            if (!candidates.isEmpty()) {
                const QJsonArray parts = candidates[0].toObject()["content"].toObject()["parts"].toArray();
                if (!parts.isEmpty()) title = parts[0].toObject()["text"].toString();
            }
        } else {
            const QJsonArray choices = obj["choices"].toArray();
            if (!choices.isEmpty()) {
                title = choices[0].toObject()["message"].toObject()["content"].toString();
            }
        }
        title = title.trimmed().remove('"').remove('\n');
        if (title.isEmpty() || title.length() > 100) return;

        ChatSession *s = m_sessionManager->session(sessionId);
        if (s) {
            s->setTitle(title);
            m_sessionManager->saveSession(s);
            m_chatPage->refreshSessionList(m_sessionManager->allSessions());
        }
    });
}
