#include "LLMProvider.h"
#include "NetworkRequestUtils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

// ============================================================================
// 构造 / 析构
// ============================================================================

LLMProvider::LLMProvider(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, [this]() {
        // 退避结束 → 用缓存的参数走 doSendStreamingRequest（不重置 m_retryAttempt）
        doSendStreamingRequest(m_lastMessages, m_lastSystemPrompt,
                               m_lastMaxTokens, m_lastTemperature);
    });
}

LLMProvider::~LLMProvider()
{
    // 析构时自动中止未完成的请求，防止悬空回调
    abort();
}

// ============================================================================
// 公共方法
// ============================================================================

void LLMProvider::sendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    // 缓存参数：5xx / 429 自动重试时复用同一份上下文，避免子类各自 cache。
    m_lastMessages = messages;
    m_lastSystemPrompt = systemPrompt;
    m_lastMaxTokens = maxTokens;
    m_lastTemperature = temperature;
    // 外部新一轮调用：清零退避计数。abort() 也会触发，但这里再写一次以防漏掉
    // 极端情况（比如 abort() 还没来得及跑就被新调用覆盖）。
    m_retryAttempt = 0;
    if (m_retryTimer) m_retryTimer->stop();
    doSendStreamingRequest(messages, systemPrompt, maxTokens, temperature);
}

/**
 * @brief 中止当前正在进行的网络请求
 *
 * 调用 QNetworkReply::abort() 取消请求，
 * 随后通过 deleteLater() 安全释放回复对象，并重置指针。
 * 同时停止退避中的重试定时器（若有），避免延迟期被 Esc 中止后还冒出请求。
 *
 * 也清空 m_pendingToolCalls：中止意味着放弃本轮交换，已解析出的 tool_use 不能
 * 再驱动 agentic 循环；更要紧的是若把它当作 assistant 消息落盘却没有对应的
 * tool_result 回传，下一次请求体会带一个悬空 tool_use，Claude 会 400。
 */
void LLMProvider::abort()
{
    if (m_retryTimer) m_retryTimer->stop();
    m_retryAttempt = 0;
    m_pendingToolCalls.clear();
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

// ============================================================================
// 受保护方法
// ============================================================================

/**
 * @brief 将网络回复对象的信号连接到本类的内部槽函数
 *
 * 子类在发起网络请求后，应调用此方法完成信号-槽的统一绑定：
 *   - readyRead     -> onReadyRead()     （逐块读取流式数据）
 *   - finished       -> onReplyFinished() （请求结束处理）
 *   - errorOccurred  -> onReplyError()    （网络错误处理）
 */
void LLMProvider::connectReplySignals(QNetworkReply *reply)
{
    m_currentReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, &LLMProvider::onReadyRead);
    connect(reply, &QNetworkReply::finished, this, &LLMProvider::onReplyFinished);
    connect(reply, &QNetworkReply::errorOccurred,
            this, &LLMProvider::onReplyError);
}

// ============================================================================
// 私有槽函数
// ============================================================================

/**
 * @brief 处理流式数据到达事件
 *
 * 工作流程：
 * 1. 从 m_currentReply 读取所有新到达的字节数据
 * 2. 将数据喂给 SSEParser，解析出完整的 SSE 事件列表
 * 3. 遍历每个事件，跳过 "done" 类型和空数据
 * 4. 调用子类实现的 parseSSEData() 把单条 event 拆成 content / reasoning 两路 token
 * 5. reasoning 走独立信号 reasoningTokenReceived（不计入 m_accumulatedResponse）
 * 6. content 累积到 m_accumulatedResponse 并发射 tokenReceived
 */
void LLMProvider::onReadyRead()
{
    if (!m_currentReply) return;

    QByteArray newData = m_currentReply->readAll();
    QList<SSEEvent> events = m_sseParser.feed(newData);

    for (const SSEEvent &event : events) {
        if (event.eventType == "done") continue;   // 流结束标记，跳过
        if (event.data.isEmpty()) continue;         // 空数据事件，跳过

        const LLMProviderParseResult result = parseSSEData(event.data);

        // reasoning 不计入 m_accumulatedResponse：只走折叠区块 + 持久化字段，
        // 不参与下一轮请求的上下文，也不会污染 TTS 朗读的 cleanResponse。
        if (!result.reasoningToken.isEmpty()) {
            emit reasoningTokenReceived(result.reasoningToken);
        }
        // 同一 event 里如果还有正文 token（rare 但合理），照样累积 + 派发。
        if (!result.contentToken.isEmpty()) {
            m_accumulatedResponse += result.contentToken;
            emit tokenReceived(result.contentToken);
        }
    }
}

/**
 * @brief 处理网络回复完成事件
 *
 * 当流式响应正常结束时，m_accumulatedResponse 已包含完整文本，直接发射。
 * 若 m_accumulatedResponse 为空，说明可能未走 SSE 流式路径（如 API 返回了
 * 非流式 JSON 错误响应），此时尝试读取剩余数据并解析错误信息。
 *
 * 最后清理回复对象和 SSE 解析器状态。
 */
void LLMProvider::onReplyFinished()
{
    if (!m_currentReply) return;

    // 未收到任何流式 token，检查是否为非 SSE 的错误响应
    if (m_accumulatedResponse.isEmpty()) {
        QByteArray remaining = m_currentReply->readAll();
        QString apiMsg = extractApiErrorMessage(remaining);
        if (!apiMsg.isEmpty()) {
            emit errorOccurred(apiMsg);
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
            m_sseParser.reset();
            return;
        }
    }

    // 正常完成，发射完整响应并清理状态
    emit responseFinished(m_accumulatedResponse);
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    m_sseParser.reset();
}

/**
 * @brief 处理网络层错误
 *
 * 优先尝试从响应体中解析 API 返回的结构化错误信息（JSON 格式），
 * 若无法解析则回退到 QNetworkReply::errorString() 提供的通用描述。
 *
 * HTTP 429 / 5xx：识别为可重试错误，按 1s / 2s / 4s 指数退避，最多 3 次；
 * 退避期间发射 retryScheduled 让 UI 显示 "Retrying... (n/N)"。耗尽重试次数
 * 或非可重试错误才走 errorOccurred 让上层报错。
 */
void LLMProvider::onReplyError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error)
    if (!m_currentReply) return;

    const int httpStatus = m_currentReply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString fallback = m_currentReply->errorString();
    const QByteArray body = m_currentReply->readAll();
    const QString errorMsg = extractApiErrorMessage(body, fallback);

    // 5xx / 429 视为暂时性故障，做退避重试；
    // 其他错误（401/403/400 等）通常重试也无效，直接报给上层。
    const bool retryable = (httpStatus == 429
                            || (httpStatus >= 500 && httpStatus <= 599));
    if (retryable && m_retryAttempt < kMaxRetries) {
        ++m_retryAttempt;
        const int delayMs = 1000 * (1 << (m_retryAttempt - 1));  // 1s, 2s, 4s

        // 清掉本次失败的请求状态，下一次 doSendStreamingRequest 会建新 reply。
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_sseParser.reset();
        m_accumulatedResponse.clear();
        m_pendingToolCalls.clear();

        emit retryScheduled(m_retryAttempt, kMaxRetries, delayMs);
        if (m_retryTimer) {
            m_retryTimer->start(delayMs);
        }
        return;
    }

    // 不可重试，或重试次数用尽：按错误处理
    emit errorOccurred(errorMsg);
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    m_sseParser.reset();
    m_accumulatedResponse.clear();
    m_pendingToolCalls.clear();
    m_retryAttempt = 0;
}

// ============================================================================
// 公共静态辅助
// ============================================================================

QString LLMProvider::extractApiErrorMessage(const QByteArray &payload,
                                            const QString &fallback)
{
    if (payload.isEmpty()) return fallback;

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return fallback;

    const QJsonObject root = doc.object();

    // 形式一：{"error": {"message": "..."}}
    if (root.value("error").isObject()) {
        const QString msg = root.value("error").toObject().value("message").toString();
        if (!msg.isEmpty()) return msg;
    }

    // 形式二：{"message": "..."}（部分代理 / 错误网关使用）
    const QString msg = root.value("message").toString();
    if (!msg.isEmpty()) return msg;

    return fallback;
}

QString LLMProvider::flattenTextAttachments(const QList<Attachment> &attachments)
{
    QString out;
    for (const auto &att : attachments) {
        if (att.type == Attachment::TextFile) {
            out += QStringLiteral("[File: %1]\n%2\n\n")
                       .arg(att.fileName, att.textContent);
        }
    }
    return out;
}

QNetworkRequest LLMProvider::makeJsonRequest(const QUrl &url)
{
    QNetworkRequest request;
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    applyGoogleChromeUserAgent(request);
    return request;
}
