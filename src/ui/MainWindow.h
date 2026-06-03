#pragma once
#include "ElaWindow.h"
#include "core/ChatSession.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QSet>

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
    void onMessageRegenerate(int index);       // 重新生成 assistant 回复（截断到 user 后重发）
    void onMessageEdit(int index);             // 编辑 user 消息：回填输入框并截断本条及之后
    void onImageGenerationStarted(int count);  // Provider 开始图像生成 → 切状态文字
    void flushPendingBubbleText();              // 把累积的 token 批量喂给气泡（80ms 节流）
    void onReasoningTokenReceived(const QString &token); // 收到 reasoning（思考链）增量
    void onToolExecFinished();                  // 工具在工作线程执行完毕 → 回 GUI 线程续传（C8）

private:
    void setupPages();
    void setupConnections();
    void createProvider();
    void loadSessions();
    void setupTachie();                         // 初始化立绘窗口
    QString buildAugmentedSystemPrompt() const; // 构建包含情绪标签指令的 system prompt
    void queuePendingBubbleText(const QString &text); // 把 token 排入流式 flush 队列
    void abortStreamingAndSavePartial();        // Esc 中止流式：保留已收到的 partial reply
    void beginStreamingForActiveSession();      // 复用：预建气泡 + 重置状态 + 发起流式请求
    /**
     * @brief 执行模型发起的工具调用，回传结果并继续 agentic 循环
     *
     * onResponseFinished 检测到本轮有未决 tool_use（且非中止）时调用：先在 GUI 线程
     * 过审批门，再把已审批工具丢到工作线程异步执行（C8，避免 fetch_url 同步阻塞冻结
     * 界面）；执行完毕由 onToolExecFinished 拼 tool_result、落盘、续传下一轮。每轮自增
     * m_agenticIterations，超过 kMaxAgenticIterations 后执行完工具即停（不再回传），
     * 防止模型与工具间失控往返。详见 MainWindow.cpp 实现处注释。
     */
    void runToolCallsAndContinue(const QList<ToolCall> &calls);

    /**
     * @brief 按 m_toolExecCalls 原始顺序拼一条 user(tool_result) 消息，落盘 + 合并渲染
     *
     * resultsById 提供每个 tool_use 对应结果（按 toolUseId）；未命中的调用兜底成一条
     * isError 结果、content 取 fallbackContent。onToolExecFinished（正常完成）与 Esc
     * 取消两条路径共用，保证产出结构一致的 tool_result，维持 tool_use/tool_result 成对。
     */
    void appendToolResultsMessage(const QHash<QString, ToolResult> &resultsById,
                                  const QString &fallbackContent);

    // ===== 工具审批（C6）=====
    /** @brief 一次工具审批的结果：拒绝 / 仅本次允许 / 本次运行内一直允许 */
    enum class ToolApproval { Denied, AllowedOnce, AllowedForSession };
    /**
     * @brief 工具执行前的审批门：ReadOnly 直接放行，其余弹审批对话框
     *
     * ShellOrNetwork 选 "Allow for session" 后把工具名记入 m_sessionApprovedTools，
     * 本次运行内同名调用不再弹。返回 true 表示可执行；false 表示用户拒绝，调用方
     * 回填一条 isError 的 tool_result，保持 tool_use/tool_result 成对、会话合法。
     */
    bool approveToolCall(const ToolCall &call);
    /** @brief 弹出跟随 ElaTheme 的工具审批对话框；Esc / 关闭 = 拒绝（安全默认） */
    ToolApproval promptToolApproval(const Tool &tool, const QString &argsJson);

    /**
     * @brief 首轮对话结束后自动生成简短会话标题
     *
     * 触发条件：onResponseFinished 收到 assistant 消息后会话总条数恰为 2
     * （user + assistant 各一条）。向当前 provider 发一次非流式短请求让模型
     * 用 4-8 词总结话题。失败时保留默认标题，不影响主流程。
     */
    void generateSessionTitle(ChatSession *session);

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

    // 流式状态文字：每轮发起前置 false，第一条正文 token 到达时翻成 true
    // 并把 "Thinking..." 切成 "Generating..."。让用户区分"模型还在憋着"和
    // "已经在出文了"。
    bool m_firstTokenSeen = false;

    // 请求计时：beginStreamingForActiveSession 时记录起点，onResponseFinished
    // 计算耗时并和粗估 token 数一起显示在状态栏 3 秒。
    QDateTime m_requestStartTime;

    // Agentic 工具循环：每个用户回合（onSendMessage / onMessageRegenerate）置 0，
    // 每次「执行工具→回传结果→再请求」自增。达到 kMaxAgenticIterations 后执行完
    // 工具即停（不再回传），防止模型与工具间无限往返。
    int m_agenticIterations = 0;
    static constexpr int kMaxAgenticIterations = 25;

    // 本次运行内已"整段允许"的工具名（审批弹窗选 "Allow for session" 后加入）。
    // 仅进程内有效，重启清空；C7 会提供持久化的按工具开关 / risk 覆盖。
    QSet<QString> m_sessionApprovedTools;

    // ===== 工具异步执行（C8）=====
    // 工具 execute 可能阻塞（fetch_url 同步 GET 最长 15s），挪到 QtConcurrent 线程池跑，
    // 主线程不冻结。审批仍在 GUI 线程（模态对话框），只有 execute 跨线程。执行期间
    // m_isStreaming 保持 true（仍属"忙"），但没有活跃网络流——Esc 路径据
    // m_toolExecInProgress 区分这一状态。
    //
    // 取消（C8b）：QtConcurrent::run 无法中断，工具会在后台跑完。Esc 做"逻辑取消"——
    // 立刻为本批 tool_use 写回 tool_result（保持成对、会话合法）并复位空闲，置
    // m_toolExecCancelled 让迟到的 onToolExecFinished 丢弃工作线程结果、不重复写。
    bool m_toolExecInProgress = false;                          // 工具正在工作线程执行
    bool m_toolExecCancelled = false;                           // Esc 已逻辑取消本批执行
    QFutureWatcher<QList<ToolResult>> *m_toolWatcher = nullptr; // 执行完成回到 GUI 线程
    QList<ToolCall> m_toolExecCalls;                            // 本批调用（原始顺序，用于重组结果）
    QHash<QString, ToolResult> m_toolExecDenied;                // 审批被拒的调用结果（按 toolUseId）

    // TTS 相关
    EdgeTTSProvider *m_ttsProvider = nullptr;    // Edge TTS 语音合成器
    QMediaPlayer *m_ttsPlayer = nullptr;         // MP3 音频播放器
    bool m_ttsPendingPlay = false;               // 合成完成等待播放的标志

    // 嘴型同步计时器（TTS 播放期间定时切换立绘表情）
    class QTimer *m_lipSyncTimer = nullptr;
    QString m_lipSyncEmotion;  // 播放前的情绪，播放结束后恢复
};
