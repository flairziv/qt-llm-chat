#include "LLMProvider.h"
#include <QJsonDocument>
#include <QJsonObject>

// ============================================================================
// 构造 / 析构
// ============================================================================

LLMProvider::LLMProvider(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
}

LLMProvider::~LLMProvider()
{
    // 析构时自动中止未完成的请求，防止悬空回调
    abort();
}

// ============================================================================
// 公共方法
// ============================================================================

/**
 * @brief 中止当前正在进行的网络请求
 *
 * 调用 QNetworkReply::abort() 取消请求，
 * 随后通过 deleteLater() 安全释放回复对象，并重置指针。
 */
void LLMProvider::abort()
{
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
 * 4. 调用子类实现的 parseSSEData() 从事件 data 中提取 token
 * 5. 将 token 累积到 m_accumulatedResponse 并发射 tokenReceived 信号
 */
void LLMProvider::onReadyRead()
{
    if (!m_currentReply) return;

    QByteArray newData = m_currentReply->readAll();
    QList<SSEEvent> events = m_sseParser.feed(newData);

    for (const SSEEvent &event : events) {
        if (event.eventType == "done") continue;   // 流结束标记，跳过
        if (event.data.isEmpty()) continue;         // 空数据事件，跳过

        QString token = parseSSEData(event.data);   // 子类负责具体解析
        if (!token.isEmpty()) {
            m_accumulatedResponse += token;
            emit tokenReceived(token);
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
 * 发射 errorOccurred 信号后清理所有请求状态。
 */
void LLMProvider::onReplyError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error)
    if (!m_currentReply) return;

    const QString fallback = m_currentReply->errorString();
    const QByteArray body = m_currentReply->readAll();
    const QString errorMsg = extractApiErrorMessage(body, fallback);

    emit errorOccurred(errorMsg);
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    m_sseParser.reset();
    m_accumulatedResponse.clear();
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
