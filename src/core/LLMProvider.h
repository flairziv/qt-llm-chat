#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QList>
#include "ChatSession.h"
#include "SSEParser.h"

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
     * @brief 发送流式聊天请求（纯虚函数，子类必须实现）
     * @param messages     聊天消息列表（包含历史上下文）
     * @param systemPrompt 系统提示词，用于设定模型行为
     * @param maxTokens    最大生成 token 数
     * @param temperature  采样温度，控制输出随机性（0.0 - 2.0）
     */
    virtual void sendStreamingRequest(
        const QList<ChatMessage> &messages,
        const QString &systemPrompt,
        int maxTokens,
        double temperature
    ) = 0;

    /** @brief 中止当前正在进行的网络请求 */
    virtual void abort();

    /** @brief 返回提供者名称（如 "OpenAI"、"DeepSeek"），子类必须实现 */
    virtual QString providerName() const = 0;

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
    /** @brief 收到一个新的流式 token 时发射 */
    void tokenReceived(const QString &token);

    /** @brief 流式响应全部完成时发射，携带完整的累积响应文本 */
    void responseFinished(const QString &fullResponse);

    /** @brief 发生错误时发射，携带错误描述信息 */
    void errorOccurred(const QString &errorMessage);

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
     * @brief 解析单条 SSE 事件的 data 字段，提取生成的文本 token（纯虚函数）
     * @param data  SSE 事件的原始 data 内容（通常为 JSON）
     * @return 解析出的文本 token，解析失败或无内容时返回空字符串
     */
    virtual QString parseSSEData(const QByteArray &data) = 0;

private slots:
    /** @brief 网络回复有新数据可读时触发，负责 SSE 事件解析与 token 分发 */
    void onReadyRead();

    /** @brief 网络回复完成时触发，处理非流式错误响应并发射 responseFinished */
    void onReplyFinished();

    /** @brief 网络层发生错误时触发，尝试从响应体中提取详细错误信息 */
    void onReplyError(QNetworkReply::NetworkError error);
};
