#pragma once
#include "ElaWindow.h"
#include "core/ChatSession.h"

#include <QNetworkAccessManager>

class AppSettings;
class SessionManager;
class ChatPage;
class CompareModePage;
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
    void onRenameChat(const QString &sessionId);
    void onExportSession(const QString &sessionId);
    void onSessionSelected(const QString &sessionId);
    void onSendMessage(const QString &text, const QList<Attachment> &attachments);
    void onTokenReceived(const QString &token);
    void onResponseFinished(const QString &fullResponse);
    void onProviderError(const QString &error);
    void onProviderSwitched(const QString &providerName);
    void onSettingsChanged();
    void onMessageFavoriteToggle(int index);   // 收藏/取消收藏单条消息
    void onMessageDeleteFromHere(int index);   // 删除该消息及其后的全部消息

private:
    void setupPages();
    void setupConnections();
    void createProvider();
    void loadSessions();
    void setupTachie();                         // 初始化立绘窗口
    QString buildAugmentedSystemPrompt() const; // 构建包含情绪标签指令的 system prompt

    AppSettings *m_settings;
    SessionManager *m_sessionManager;
    QNetworkAccessManager *m_nam;
    LLMProvider *m_provider = nullptr;
    bool m_isStreaming = false;

    // 页面指针
    ChatPage *m_chatPage;
    CompareModePage *m_comparePage;
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

    // TTS 相关
    EdgeTTSProvider *m_ttsProvider = nullptr;    // Edge TTS 语音合成器
    QMediaPlayer *m_ttsPlayer = nullptr;         // MP3 音频播放器
    bool m_ttsPendingPlay = false;               // 合成完成等待播放的标志

    // 嘴型同步计时器（TTS 播放期间定时切换立绘表情）
    class QTimer *m_lipSyncTimer = nullptr;
    QString m_lipSyncEmotion;  // 播放前的情绪，播放结束后恢复
};
