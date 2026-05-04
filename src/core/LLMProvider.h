#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QList>
#include <QString>
#include "ChatSession.h"
#include "SSEParser.h"

class QTimer;

/**
 * @brief 单个 SSE 事件解析结果
 *
 * parseSSEData() 把单条 SSE event 的 data 拆成正文 token + reasoning token，
 * 两者可同时非空（部分代理 / 实验性 API 会在同一 event 里既给 content 又给
 * reasoning_content；分离两路就不会丢正文导致 TTS 失静）。基类 onReadyRead
 * 看见哪一路非空就 emit 对应的信号；都为空时整条 event 丢掉。
 */
struct LLMProviderParseResult {
    QString contentToken;
    QString reasoningToken;
};

/**
 * @brief LLM 服务提供者的抽象基类
 *
 * 定义了与大语言模型（LLM）交互的通用接口，支持流式（SSE）请求。
 * 子类需实现具体的请求发送逻辑和 SSE 数据解析逻辑，
 * 基类负责管理网络回复的信号连接、SSE 事件分发和错误处理。
 */
class LLMProvider : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param nam  共享的网络访问管理器，由外部创建并管理生命周期
     * @param parent  父 QObject
     */
    explicit LLMProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    /** @brief 析构函数，自动中止正在进行的请求 */
    virtual ~LLMProvider();

    /**
     * @brief 发送流式聊天请求（基类提供具体实现，封装重试逻辑）
     * @param messages     聊天消息列表（包含历史上下文）
     * @param systemPrompt 系统提示词，用于设定模型行为
     * @param maxTokens    最大生成 token 数
     * @param temperature  采样温度，控制输出随机性（0.0 - 2.0）
     *
     * 行为：
     *   - 缓存请求参数（用于 5xx / 429 自动重试）
     *   - 把 m_retryAttempt 重置为 0
     *   - 通过 doSendStreamingRequest() 派发给子类去发起真正的网络请求
     *
     * 子类只需实现 doSendStreamingRequest()，不应再覆盖此方法。
     */
    void sendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    );

    /** @brief 中止当前正在进行的网络请求 */
    virtual void abort();

    /** @brief 返回提供者名称（如 "OpenAI"、"DeepSeek"），子类必须实现 */
    virtual QString providerName() const = 0;

    /** @brief 获取当前已累积的流式响应文本（用于中止时保存部分回复） */
    QString accumulatedResponse() const { return m_accumulatedResponse; }

    /**
     * @brief 从 API 错误响应体中提取人类可读的 message
     *
     * 三家主流 LLM 厂商错误响应格式都是 `{"error":{"message":"..."}}` 或顶层 `{"message":"..."}`，
     * 这里统一解析。解析失败或无对应字段时返回 fallback。
     *
     * @param payload  HTTP 响应体原始字节（可能是 JSON、HTML、或空）
     * @param fallback 解析不到时的回退文案（如 QNetworkReply::errorString()）
     */
    static QString extractApiErrorMessage(const QByteArray &payload,
                                          const QString &fallback = {});

    /**
     * @brief 将消息附件中的 TextFile 部分拼接为可发给 LLM 的纯文本块
     *
     * 三家 Provider 都以 `[File: 文件名]\n文件内容\n\n` 的格式把文本附件拼到 user 消息体里，
     * 抽到基类避免散落实现漂移。仅处理 TextFile，Image / Document 由各 Provider 按本家 API 形态另行处理。
     */
    static QString flattenTextAttachments(const QList<Attachment> &attachments);

    /**
     * @brief 构造一个发往 LLM API 的标准 JSON 请求对象
     *
     * 统一三家 Provider 都需要的请求基础设置：
     *   - Content-Type: application/json
     *   - 显式禁用 HTTP/2（部分代理对 HTTP/2 + SSE 兼容性差）
     *   - 伪装 Chrome 浏览器 User-Agent（绕过部分网关的简单 UA 检查）
     *
     * 鉴权头（Authorization / x-api-key / anthropic-version 等）和传输超时
     * 由调用方根据自家 API 形态自行追加。
     */
    static QNetworkRequest makeJsonRequest(const QUrl &url);

signals:
    /** @brief 收到一个新的流式正文 token 时发射 */
    void tokenReceived(const QString &token);

    /**
     * @brief 收到 reasoning（思考链）token 时发射
     *
     * Claude 的 thinking_delta、OpenAI / DeepSeek 的 delta.reasoning_content
     * 走这个通道，与正文 tokenReceived 分开传输。MainWindow 把 reasoning 缓冲
     * 起来作为消息的独立字段持久化、并驱动 MessageBubble 的折叠区块显示。
     */
    void reasoningTokenReceived(const QString &token);

    /** @brief 流式响应全部完成时发射，携带完整的累积响应文本 */
    void responseFinished(const QString &fullResponse);

    /** @brief 发生错误时发射，携带错误描述信息 */
    void errorOccurred(const QString &errorMessage);

    /**
     * @brief 图片生成请求已发出但响应未到（用于 UI 显示 shimmer 占位符）
     *
     * 在 OpenAIProvider 的图片路径即将 POST 请求时发射；普通对话流式不发射。
     * count 表示预计生成的图片数量。
     */
    void imageGenerationStarted(int count);

    /**
     * @brief 自动重试已排定（HTTP 429 / 5xx）
     *
     * 网络层错误被识别为「可重试」时，基类不会立即 emit errorOccurred，
     * 而是先发射这个信号、然后按 attempt 序数做指数退避（1s / 2s / 4s）后
     * 重新调用 doSendStreamingRequest()。MainWindow 监听此信号显示
     * "Retrying... (n/N)" 状态文字并清空当前 assistant 气泡内容。
     *
     * 若 abort()（含 Esc 中止 / settings 改变触发的 createProvider）在退避
     * 期间被调用，定时器会被停掉，此后不会再 emit retryScheduled，也不会
     * 再 emit errorOccurred —— 调用方负责自己处理 UI 状态。
     */
    void retryScheduled(int attempt, int maxAttempts, int delayMs);

protected:
    QNetworkAccessManager *m_nam;              // 共享的网络访问管理器
    QNetworkReply *m_currentReply = nullptr;   // 当前活跃的网络回复对象
    QString m_accumulatedResponse;             // 累积的完整响应文本
    SSEParser m_sseParser;                     // SSE 事件流解析器

    /**
     * @brief 将网络回复的信号连接到内部槽函数
     * @param reply  要连接的 QNetworkReply 对象
     *
     * 连接 readyRead、finished、errorOccurred 三个信号，
     * 并将 reply 保存到 m_currentReply。
     */
    void connectReplySignals(QNetworkReply *reply);

    /**
     * @brief 子类实现的实际网络请求发起逻辑（template-method 模式的钩子）
     *
     * 公共 sendStreamingRequest() 负责缓存参数 + 重置重试计数后调用此方法。
     * 自动重试路径在退避结束后也直接调用此方法（不重置计数）。子类只关心
     * "构造 JSON、加鉴权头、POST"，无需感知重试机制。
     */
    virtual void doSendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) = 0;

    /**
     * @brief 解析单条 SSE 事件的 data 字段，提取文本 token 和 reasoning token
     * @param data  SSE 事件的原始 data 内容（通常为 JSON）
     * @return ParseResult { contentToken, reasoningToken }；都为空表示该事件无产出
     */
    virtual LLMProviderParseResult parseSSEData(const QByteArray &data) = 0;

private:
    // --- 自动重试状态 ---
    // 缓存上一次 sendStreamingRequest 的参数，用于退避后重新触发
    // doSendStreamingRequest() 时使用同样的上下文。
    QList<ChatMessage> m_lastMessages;
    QString m_lastSystemPrompt;
    int m_lastMaxTokens = 0;
    double m_lastTemperature = 0.0;
    int m_retryAttempt = 0;
    QTimer *m_retryTimer = nullptr;
    static constexpr int kMaxRetries = 3;

private slots:
    /** @brief 网络回复有新数据可读时触发，负责 SSE 事件解析与 token 分发 */
    void onReadyRead();

    /** @brief 网络回复完成时触发，处理非流式错误响应并发射 responseFinished */
    void onReplyFinished();

    /** @brief 网络层发生错误时触发，尝试从响应体中提取详细错误信息 */
    void onReplyError(QNetworkReply::NetworkError error);
};
