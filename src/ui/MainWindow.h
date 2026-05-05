#pragma once
#include "ElaWindow.h"
#include "core/ChatSession.h"

#include <QNetworkAccessManager>

class AppSettings;
class SessionManager;
class ChatPage;
class AnalyticsPage;
class AsciiArtPage;
class SettingClaudePage;
class SettingOpenAIPage;
class SettingGeminiPage;
class SettingGeneralPage;
class LLMProvider;
class ChatSession;
class TachieWindow;
class EdgeTTSProvider;
class QMediaPlayer;

/**
 * @brief 应用主窗口 -- 核心调度中心
 *
 * 继承自 ElaWindow（ElaWidgetTools 提供的无边框美化窗口），
 * 负责将各个独立模块（UI 页面、LLM Provider、会话管理器、立绘窗口）串联起来：
 *
 *   ┌──────────────────────────────────────────────────┐
 *   │  MainWindow（调度中心）                            │
 *   │                                                  │
 *   │  ChatPage ←─信号─→ MainWindow ←─信号─→ LLMProvider
 *   │                        ↕                         │
 *   │               SessionManager                     │
 *   │                        ↕                         │
 *   │             SettingXxxPage（多个）                 │
 *   │                        ↕                         │
 *   │              TachieWindow（立绘联动）              │
 *   └──────────────────────────────────────────────────┘
 */
class MainWindow : public ElaWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppSettings *settings, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onNewChat();
    void onDeleteChat(const QString &sessionId);
    void onRenameChat(const QString &sessionId, const QString &newName);
    void onExportSession(const QString &sessionId);
    void onSessionSelected(const QString &sessionId);
    void onSendMessage(const QString &text, const QList<Attachment> &attachments);
    void onTokenReceived(const QString &token);
    void onResponseFinished(const QString &fullResponse);
    void onProviderError(const QString &error);
    void onProviderSwitched(const QString &providerName);
    void onProviderSettingsChanged();
    void onUiSettingsChanged();
    void onMessageFavoriteToggle(int index);   // 收藏/取消收藏单条消息
    void onMessageDeleteFromHere(int index);   // 删除该消息及其后的全部消息
    void flushPendingBubbleText();              // 把累积的 token 批量喂给气泡（80ms 节流）
    void onReasoningTokenReceived(const QString &token); // 收到 reasoning（思考链）增量

private:
    void setupPages();
    void setupConnections();
    void createProvider();
    void loadSessions();
    void setupTachie();                         // 初始化立绘窗口
    QString buildAugmentedSystemPrompt() const; // 构建包含情绪标签指令的 system prompt
    void queuePendingBubbleText(const QString &text); // 把 token 排入流式 flush 队列

    AppSettings *m_settings;
    SessionManager *m_sessionManager;
    QNetworkAccessManager *m_nam;
    LLMProvider *m_provider = nullptr;
    bool m_isStreaming = false;

    // 页面指针
    ChatPage *m_chatPage;
    AnalyticsPage *m_analyticsPage;
    AsciiArtPage *m_asciiArtPage;
    SettingClaudePage *m_claudePage;
    SettingOpenAIPage *m_openaiPage;
    SettingGeminiPage *m_geminiPage;
    SettingGeneralPage *m_generalPage;

    // 立绘相关
    TachieWindow *m_tachieWindow = nullptr;     // 立绘窗口（独立顶层窗口）
    bool m_tachieEnabled = false;               // 是否启用立绘联动
    bool m_emotionTagParsed = false;            // 当前回复的情绪标签是否已解析
    QString m_tokenBuffer;                      // 缓冲前几个 token 用于提取情绪标签

    // 流式 flush 节流：每 token 直接 setText 在长回复里 QLabel relayout 开销很大；
    // 累积到 m_pendingBubbleText，由 80ms single-shot 定时器批量刷新一次。
    class QTimer *m_streamFlushTimer = nullptr;
    QString m_pendingBubbleText;

    // Reasoning（思考链）缓冲：reasoningTokenReceived 流入，onResponseFinished
    // 时写入 ChatMessage::reasoning 一并持久化。本轮回复内累积，新一轮重置。
    QString m_reasoningBuffer;

    // TTS 相关
    EdgeTTSProvider *m_ttsProvider = nullptr;    // Edge TTS 语音合成器
    QMediaPlayer *m_ttsPlayer = nullptr;         // MP3 音频播放器
    bool m_ttsPendingPlay = false;               // 合成完成等待播放的标志

    // 嘴型同步计时器（TTS 播放期间定时切换立绘表情）
    class QTimer *m_lipSyncTimer = nullptr;
    QString m_lipSyncEmotion;  // 播放前的情绪，播放结束后恢复
};
