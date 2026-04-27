#include "MainWindow.h"
#include "ChatPage.h"
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
#include <QShortcut>
#include <QMediaPlayer>
#include <QDebug>
#include <QTimer>
#include <QRandomGenerator>

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
 *   SettingXxxPage ──settingsChanged──→ MainWindow::onSettingsChanged
 *                                           ↓ 重建 Provider
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
    connect(m_chatPage, &ChatPage::providerSwitched, this, &MainWindow::onProviderSwitched);
    connect(m_chatPage, &ChatPage::messageFavoriteToggleRequested,
            this, &MainWindow::onMessageFavoriteToggle);
    connect(m_chatPage, &ChatPage::messageDeleteFromHereRequested,
            this, &MainWindow::onMessageDeleteFromHere);

    // === 设置页变更信号 ===
    connect(m_claudePage, &SettingClaudePage::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_openaiPage, &SettingOpenAIPage::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_geminiPage, &SettingGeminiPage::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_generalPage, &SettingGeneralPage::settingsChanged, this, &MainWindow::onSettingsChanged);

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
    m_sessionManager->deleteSession(sessionId);

    ChatSession *active = m_sessionManager->activeSession();
    if (active) {
        m_chatPage->loadMessages(active->messages());
    } else {
        m_chatPage->clearMessages();
    }
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
void MainWindow::onSendMessage(const QString &text)
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
    session->addMessage(userMsg);
    m_chatPage->addMessageBubble("user", text);

    // 预创建空的 assistant 气泡，后续 onTokenReceived 会逐步填充
    m_chatPage->addMessageBubble("assistant", "");

    // 进入流式状态
    m_isStreaming = true;
    m_chatPage->setInputEnabled(false);
    m_chatPage->setStatusText("Thinking...");

    // 重置情绪标签解析状态（每轮回复重新解析）
    m_emotionTagParsed = false;
    m_tokenBuffer.clear();

    // 重置 TTS 状态（中止上一轮朗读，预连接为新一轮做准备）
    m_ttsProvider->abort();
    m_ttsPendingPlay = false;
    m_ttsPlayer->stop();
    m_ttsPlayer->setMedia(QMediaContent());
    if (m_settings->ttsEnabled()) {
        m_ttsProvider->preConnect();
    }

    // 发起流式请求（立绘启用时使用包含情绪指令的 system prompt）
    m_provider->sendStreamingRequest(
        session->messages(),
        buildAugmentedSystemPrompt(),
        m_settings->maxTokens(),
        m_settings->temperature()
    );
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
    // 立绘关闭或标签已解析 → 直接转发给 ChatPage
    if (!m_tachieEnabled || !m_tachieWindow || m_emotionTagParsed) {
        m_chatPage->appendToLastBubble(token);
        return;
    }

    // 缓冲 token 用于提取情绪标签
    m_tokenBuffer += token;

    // 检查缓冲中是否包含 ']'（标签结束符）
    int closeBracket = m_tokenBuffer.indexOf(']');
    if (closeBracket >= 0) {
        m_emotionTagParsed = true;

        // 用正则提取 [情绪名]
        QRegularExpression re(QStringLiteral("^\\s*\\[([^\\]]+)\\]"));
        QRegularExpressionMatch match = re.match(m_tokenBuffer);

        if (match.hasMatch()) {
            QString emotion = match.captured(1);
            m_tachieWindow->changeExpression(emotion);

            // 标签之后的文本转发给气泡
            QString remaining = m_tokenBuffer.mid(match.capturedEnd());
            if (!remaining.isEmpty()) {
                m_chatPage->appendToLastBubble(remaining);
            }
        } else {
            // 有 ']' 但不匹配标签格式 → 整个缓冲作为普通文本转发
            m_chatPage->appendToLastBubble(m_tokenBuffer);
        }
    } else if (m_tokenBuffer.length() > 50) {
        // 缓冲超过 50 字符仍无 ']' → 无标签，整体转发
        m_emotionTagParsed = true;
        m_chatPage->appendToLastBubble(m_tokenBuffer);
    }
    // 否则继续缓冲，等待更多 token
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
    m_chatPage->setStatusText("");

    // 从完整回复中剥离开头的 [情绪名] 标签
    QString cleanResponse = fullResponse;
    if (m_tachieEnabled && m_tachieWindow) {
        QRegularExpression re(QStringLiteral("^\\s*\\[[^\\]]+\\]\\s*"));
        cleanResponse = fullResponse;
        cleanResponse.remove(re);

        // 替换气泡内容为干净文本（去除标签）
        m_chatPage->replaceLastBubbleContent(cleanResponse);
    }

    // 保存 assistant 消息并持久化会话（保存干净文本，不含情绪标签）
    ChatSession *session = m_sessionManager->activeSession();
    if (session) {
        ChatMessage assistantMsg;
        assistantMsg.role = "assistant";
        assistantMsg.content = cleanResponse;
        session->addMessage(assistantMsg);
        m_sessionManager->saveSession(session);
    }

    // TTS 朗读 AI 回复（预连接已就绪，直接发 SSML 零延迟）
    if (m_settings->ttsEnabled() && !cleanResponse.isEmpty()) {
        m_ttsProvider->synthesize(cleanResponse, m_settings->ttsVoice());
    }
}

/** @brief LLM 请求出错，恢复输入状态并显示错误信息 */
void MainWindow::onProviderError(const QString &error)
{
    m_isStreaming = false;
    m_chatPage->setInputEnabled(true);
    m_chatPage->setStatusText("Error: " + error);
}

// ============================================================================
// 槽函数 —— 设置变更
// ============================================================================

/** @brief 任一设置页参数变更后，重建 Provider 以应用新配置 */
void MainWindow::onSettingsChanged()
{
    createProvider();
    m_chatPage->updateRoleNames(m_settings->userName(), m_settings->assistantName());

    // 立绘角色可能变化，更新资源目录
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
