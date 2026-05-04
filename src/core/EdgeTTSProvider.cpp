#include "EdgeTTSProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QLocale>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUuid>
#include <QTimer>
#include <QDebug>
#include <QStandardPaths>
#include <QFile>

// ---- 常量（与 edge-tts Python 包保持一致） ----

static const char *TRUSTED_CLIENT_TOKEN = "6A5AA1D4EAFF4E9FB37E23D68491D6F4"; // 微软受信任客户端令牌，用于 URL 鉴权与 DRM 哈希
static const char *CHROMIUM_FULL_VERSION = "143.0.3650.75";  // 完整版本号，用于 Sec-MS-GEC-Version 参数
static const char *CHROMIUM_MAJOR_VERSION = "143";           // 主版本号，用于构造 User-Agent 伪装 Edge 浏览器
static const char *BASE_URL =                                // Bing 语音合成 WebSocket 端点
    "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1";

// Windows 文件时间纪元偏移（1601-01-01 到 1970-01-01，共 11644473600 秒）
static const qint64 WIN_EPOCH = 11644473600LL;

// ---- 构造/析构 ----

EdgeTTSProvider::EdgeTTSProvider(QObject *parent)
    : QObject(parent)
{
}

EdgeTTSProvider::~EdgeTTSProvider()
{
    abort();
}

// ---- 工具函数 ----

/** @brief 生成无连字符的 UUID 字符串，用作 ConnectionId 和 RequestId。 */
QString EdgeTTSProvider::generateUUID()
{
    return QUuid::createUuid().toString().remove('{').remove('}').remove('-');
}

/** @brief 转义 XML 特殊字符（& < > " '），防止 SSML 注入。 */
QString EdgeTTSProvider::xmlEscape(const QString &s)
{
    QString r = s;
    r.replace('&',  "&amp;");
    r.replace('<',  "&lt;");
    r.replace('>',  "&gt;");
    r.replace('"',  "&quot;");
    r.replace('\'', "&apos;");
    return r;
}

/**
 * 生成 Sec-MS-GEC 令牌（DRM 验证）
 * 算法来自 https://github.com/rany2/edge-tts/blob/master/src/edge_tts/drm.py
 *
 * 关键点：5 分钟边界。如果客户端时钟与微软服务端偏离到落进不同的 5 分钟窗口，
 * 哈希就对不上、连接被拒。通过 m_clockSkewSec 把"用来算令牌的时间"前后挪几个
 * 5 分钟窗口，直到 MS 能接受。connectWs / onDisconnected 配合做窗口扫描。
 */
QString EdgeTTSProvider::generateSecMsGec()
{
    // 当前 Unix 时间戳（秒）+ 时钟偏移（默认 0）
    qint64 ticks = QDateTime::currentSecsSinceEpoch() + m_clockSkewSec;

    // 转换为 Windows 文件时间纪元
    ticks += WIN_EPOCH;

    // 向下取整到最近的 5 分钟（300 秒）
    ticks -= ticks % 300;

    // 转换为 100 纳秒间隔（Windows 文件时间格式）
    qint64 hns = ticks * 10000000LL; // 1秒 = 10^7 * 100ns

    // 拼接字符串并计算 SHA-256
    QByteArray strToHash = QString("%1%2")
        .arg(hns)
        .arg(TRUSTED_CLIENT_TOKEN)
        .toLatin1();

    QByteArray hash = QCryptographicHash::hash(strToHash, QCryptographicHash::Sha256);
    return QString(hash.toHex()).toUpper();
}

/** @brief 生成随机 16 字节十六进制字符串，作为 Cookie 中的 muid 值。 */
QString EdgeTTSProvider::generateMuid()
{
    QByteArray bytes(16, 0);
    QRandomGenerator *rng = QRandomGenerator::global();
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<char>(rng->bounded(256));
    return QString(bytes.toHex()).toUpper();
}

/**
 * @brief 生成 X-Timestamp 头要的 JS 风格日期字符串。
 *
 * 格式："Sun May 03 2026 08:30:45 GMT+0000 (Coordinated Universal Time)"。
 * 微软 Edge 的 readaloud 服务这里硬性检查格式（虽然时区文本是冗余的）；
 * 用 QLocale::c() 强制英文月/日缩写，避免本地化语言造成解析失败。
 */
QString EdgeTTSProvider::dateToString()
{
    return QLocale::c().toString(
        QDateTime::currentDateTimeUtc(),
        QStringLiteral("ddd MMM dd yyyy HH:mm:ss"))
        + QStringLiteral(" GMT+0000 (Coordinated Universal Time)");
}

// ---- 连接管理 ----

/**
 * @brief 创建 WebSocket 并发起连接（内部方法）
 *
 * 构建带 DRM 参数的 URL，伪装 Edge 浏览器请求头，发起 WebSocket 连接。
 * 连接成功后 onConnected() 会发送 config 并标记 m_ready。
 */
void EdgeTTSProvider::connectWs()
{
    // 清理旧连接
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->close();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
    m_ready = false;

    // qDebug() << "[TTS] Connecting to Edge TTS WebSocket...";
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(m_ws, &QWebSocket::connected,
            this, &EdgeTTSProvider::onConnected);
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &EdgeTTSProvider::onTextMessageReceived);
    connect(m_ws, &QWebSocket::binaryMessageReceived,
            this, &EdgeTTSProvider::onBinaryMessageReceived);
    connect(m_ws, &QWebSocket::disconnected,
            this, &EdgeTTSProvider::onDisconnected);
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &EdgeTTSProvider::onWsError);

    // 构建带 DRM 参数的 URL
    QString connId = generateUUID();
    QString secMsGec = generateSecMsGec();
    QString secMsGecVersion = QString("1-%1").arg(CHROMIUM_FULL_VERSION);
    QString urlStr = QString("%1?TrustedClientToken=%2&ConnectionId=%3"
                             "&Sec-MS-GEC=%4&Sec-MS-GEC-Version=%5")
        .arg(BASE_URL, TRUSTED_CLIENT_TOKEN, connId, secMsGec, secMsGecVersion);

    // 构建 HTTP 请求头，伪装为 Edge 浏览器的"大声朗读"扩展
    QUrl url(urlStr);
    QNetworkRequest request{url};
    QString ua = QString("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
                         " (KHTML, like Gecko) Chrome/%1.0.0.0 Safari/537.36"
                         " Edg/%1.0.0.0").arg(CHROMIUM_MAJOR_VERSION);

    request.setRawHeader("User-Agent", ua.toLatin1());
    request.setRawHeader("Origin", "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold");
    request.setRawHeader("Pragma", "no-cache");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Accept-Encoding", "gzip, deflate, br, zstd");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setRawHeader("Cookie", QString("muid=%1;").arg(generateMuid()).toLatin1());

    m_ws->open(request);
}

/**
 * @brief 预建立 WebSocket 连接
 *
 * 若已连接/正在连接则不重复操作。
 * 连接成功后发送 config，使后续 synthesize() 可零延迟发送 SSML。
 */
void EdgeTTSProvider::preConnect()
{
    m_aborted = false;  // 无条件重置 abort 标志，允许自动重连
    if (m_ready || m_ws) return;  // 已就绪或正在连接中
    connectWs();
}

// ---- 合成入口 ----

void EdgeTTSProvider::synthesize(const QString &text, const QString &voiceName)
{
    // qDebug() << "[TTS] synthesize() called, text length:" << text.length()
    //          << "current state - ready:" << m_ready
    //          << "synthesizing:" << m_synthesizing
    //          << "ws state:" << (m_ws ? m_ws->state() : -1);

    // 若上次合成仍在进行，先中止并重连
    if (m_synthesizing) {
        // qDebug() << "[TTS] Aborting previous synthesis";
        abort();
    }

    m_aborted = false;  // 开始新合成，允许自动重连
    m_audioBuffer.clear();  // 清空音频缓冲
    m_pendingText  = text;
    m_pendingVoice = voiceName;

    // 检查连接状态：若 WebSocket 已断开，重新建连
    if (m_ws && m_ws->state() != QAbstractSocket::ConnectedState) {
        // qDebug() << "[TTS] WebSocket not connected, reconnecting...";
        m_ready = false;
        m_ws->deleteLater();
        m_ws = nullptr;
    }

    if (m_ready && m_ws) {
        // 连接已就绪 → 直接发送 SSML（零延迟）
        // qDebug() << "[TTS] Connection ready, sending SSML immediately";
        m_synthesizing = true;
        m_ready = false;
        emit synthesisStarted();
        m_requestId = generateUUID();
        sendSSML(text, voiceName);
    } else {
        // 需要先建连，onConnected 会自动发送 pending 的 SSML
        // qDebug() << "[TTS] Need to connect first";
        connectWs();
    }
}

/** @brief 中止合成：重置状态、清空缓冲区、关闭 WebSocket。 */
void EdgeTTSProvider::abort()
{
    m_synthesizing = false;
    m_ready = false;
    m_aborted = true;   // 阻止 onDisconnected 自动重连
    m_consecutiveFailures = 0;  // 下一次 synthesize() / preConnect() 重新计数；
                                // 不动 m_clockSkewSec，沿用已找到的有效偏移
    m_pendingText.clear();
    m_pendingVoice.clear();
    m_audioBuffer.clear();
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->close();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
}

// ---- WebSocket 回调 ----

/**
 * @brief WebSocket 连接建立后的回调：发送 config，标记就绪。
 *
 * 若有 pending 的合成请求（m_pendingText 非空），立即发送 SSML。
 * 否则仅标记 m_ready = true，等待后续 synthesize() 调用。
 */
void EdgeTTSProvider::onConnected()
{
    // qDebug() << "[TTS] WebSocket connected, sending config...";
    // 握手成功 → 当前的 m_clockSkewSec / Sec-MS-GEC 是有效的，清零失败计数器
    m_consecutiveFailures = 0;
    sendConfig();

    if (!m_pendingText.isEmpty()) {
        // 有待合成文本 → 立即发送 SSML
        // qDebug() << "[TTS] Sending pending SSML, text length:" << m_pendingText.length();
        m_synthesizing = true;
        m_ready = false;
        emit synthesisStarted();
        m_requestId = generateUUID();
        sendSSML(m_pendingText, m_pendingVoice);
    } else {
        // 纯预连接，标记就绪
        // qDebug() << "[TTS] Connection ready (idle)";
        m_ready = true;
        emit connectionReady();
    }
}

/** @brief 发送音频配置：指定输出格式为 24kHz 48kbps 单声道 MP3。 */
void EdgeTTSProvider::sendConfig()
{
    // X-Timestamp 头是 MS readaloud 必填项；2024 下半年起没有该头服务端会
    // 接受 WebSocket upgrade 但在收到 speech.config / ssml 后静默关闭连接。
    QString msg = QString(
        "X-Timestamp:%1\r\n"
        "Content-Type:application/json; charset=utf-8\r\n"
        "Path:speech.config\r\n\r\n"
        "{\"context\":{\"synthesis\":{\"audio\":{"
        "\"metadataoptions\":{\"sentenceBoundaryEnabled\":\"false\","
        "\"wordBoundaryEnabled\":\"false\"},"
        "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\"}}}}\r\n")
        .arg(dateToString());
    m_ws->sendTextMessage(msg);
}

/** @brief 构造 SSML 文档并发送。 */
void EdgeTTSProvider::sendSSML(const QString &text, const QString &voiceName)
{
    // 上游 edge-tts 把 xml:lang 硬编码为 en-US（即使是中文 voice），同时把文本
    // 包在 <prosody> 里（pitch/rate/volume 都是 +0 的"无变化"值）。这里完全
    // 照搬避免任何 schema 偏差导致 MS 拒绝。
    //
    // 注意 prosody 的 rate / volume 值含 '%' —— 不能走 QString::arg() 拼接，
    // 否则 '%' 会被误认成位置占位符。改用字符串连接。
    const QString ssml =
        QStringLiteral("<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>")
        + QStringLiteral("<voice name='") + voiceName + QStringLiteral("'>")
        + QStringLiteral("<prosody pitch='+0Hz' rate='+0%' volume='+0%'>")
        + xmlEscape(text)
        + QStringLiteral("</prosody></voice></speak>");

    // 注意：X-Timestamp 末尾的 'Z' 是 MS 的 bug（上游注释 "This is not a mistake,
    // Microsoft Edge bug." 强调要保留）。不加这个 'Z' 服务端会拒掉 SSML。
    const QString msg =
        QStringLiteral("X-RequestId:") + m_requestId + QStringLiteral("\r\n")
        + QStringLiteral("Content-Type:application/ssml+xml\r\n")
        + QStringLiteral("X-Timestamp:") + dateToString() + QStringLiteral("Z\r\n")
        + QStringLiteral("Path:ssml\r\n\r\n")
        + ssml;

    m_ws->sendTextMessage(msg);
}

/**
 * @brief 接收二进制音频数据并立即转发。
 *
 * 消息格式：前 2 字节为大端序头部长度，之后是头部内容,再之后是实际 PCM 音频数据。
 * 跳过头部后，通过 audioChunkReceived 信号将 PCM 数据直接推送给调用方，
 * 调用方可立即写入 QAudioOutput 实现流式播放，无需等待合成完成。
 */
void EdgeTTSProvider::onBinaryMessageReceived(const QByteArray &message)
{
    if (!m_synthesizing || message.size() < 2)
        return;

    quint16 headerLen = (static_cast<quint8>(message[0]) << 8)
                      |  static_cast<quint8>(message[1]);
    int audioOffset = 2 + headerLen;

    if (audioOffset < message.size()) {
        QByteArray chunk = message.mid(audioOffset);
        m_audioBuffer.append(chunk);
        // qDebug() << "[TTS] Audio chunk received, size:" << chunk.size()
        //          << "total:" << m_audioBuffer.size() << "bytes";
        emit audioChunkReceived(chunk);
    }
}

/**
 * @brief 接收文本消息。当检测到 "Path:turn.end" 时，表示合成完成。
 *
 * 将累积的 MP3 音频数据写入临时文件，保持连接复用，通过 synthesisFinished 信号返回文件路径。
 */
void EdgeTTSProvider::onTextMessageReceived(const QString &message)
{
    // if (message.contains("Path:turn.start")) {
    //     qDebug() << "[TTS] turn.start received";
    // }
    if (!message.contains("Path:turn.end"))
        return;

    // qDebug() << "[TTS] turn.end received, synthesis complete, total audio:" << m_audioBuffer.size() << "bytes";
    m_synthesizing = false;
    m_pendingText.clear();      // 合成完成，清掉 pending 防止重连时重复发送
    m_pendingVoice.clear();

    if (m_audioBuffer.isEmpty()) {
        emit errorOccurred("未收到任何音频数据");
        m_ready = true;  // 保持连接
        return;
    }

    // 写入临时 MP3 文件（用时间戳避免文件名冲突）
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString filePath = tmpDir + QString("/llmchat_tts_%1.mp3").arg(QDateTime::currentMSecsSinceEpoch());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred("无法写入临时文件: " + filePath);
        m_ready = true;
        return;
    }
    file.write(m_audioBuffer);
    file.close();

    // qDebug() << "[TTS] MP3 file written:" << filePath;

    // 保持连接，标记就绪，下次 synthesize() 可零延迟复用
    m_ready = true;

    emit synthesisFinished(filePath);
}

/** @brief 连接断开回调。若合成中则报错，否则按退避策略尝试重连。 */
void EdgeTTSProvider::onDisconnected()
{
    // qDebug() << "[TTS] WebSocket disconnected, was synthesizing:" << m_synthesizing
    //          << "was ready:" << m_ready
    //          << "close code:" << (m_ws ? m_ws->closeCode() : 0)
    //          << "close reason:" << (m_ws ? m_ws->closeReason() : "N/A");

    m_ready = false;
    if (m_ws) {
        m_ws->deleteLater();
        m_ws = nullptr;
    }
    if (m_synthesizing) {
        m_synthesizing = false;
        emit errorOccurred("WebSocket 连接意外断开");
        return;
    }
    if (m_aborted) {
        return;  // 调用方主动中止，不要再启动重连
    }

    // 自动重连策略：最多 2 次尝试
    //   attempt 1: 把 m_clockSkewSec 挪到 -300（防客户端时钟刚跨过 5 分钟边界
    //              而服务端还没跨）
    //   attempt 2: 把 m_clockSkewSec 复位到 0（万一刚才 -300 是不必要的）
    //   超出后放弃 —— 大概率是网络 / 区域级别的问题，再多重试只会让 MS 把 IP
    //   暂时拉黑，反而让 synthesize() 也连不上。
    ++m_consecutiveFailures;
    if (m_consecutiveFailures > 2) {
        qWarning() << "[TTS] Auto-reconnect 放弃。如果一直 RemoteHostClosed / 403，"
                      "通常是 speech.platform.bing.com 在你这边走不通："
                      "需要走代理 / VPN，或者关掉 TTS。"
                   << "consecutive failures:" << (m_consecutiveFailures - 1);
        m_consecutiveFailures = 0;
        m_clockSkewSec = 0;
        return;
    }

    // 第一次失败试 skew = -300（上一个 5 分钟窗口），第二次失败回到 skew = 0
    m_clockSkewSec = (m_consecutiveFailures == 1) ? -300 : 0;

    const int backoffMs = 1000 * (1 << (m_consecutiveFailures - 1));   // 1s, 2s
    qWarning() << "[TTS] Reconnect attempt" << m_consecutiveFailures
               << "of 2 after" << backoffMs << "ms (skew now"
               << m_clockSkewSec << "s)";
    QTimer::singleShot(backoffMs, this, [this]() {
        if (!m_ws && !m_synthesizing && !m_aborted) {
            connectWs();
        }
    });
}

/** @brief WebSocket 错误回调，提取错误描述并通过 errorOccurred 信号通知。 */
void EdgeTTSProvider::onWsError(QAbstractSocket::SocketError error)
{
    QString errMsg = m_ws ? m_ws->errorString() : "未知错误";
    qWarning() << "[TTS] WebSocket error:" << error << errMsg;

    // HTTP 403 = MS 明确拒绝授权（IP 被临时拉黑 / 区域屏蔽 / token 反复错位都会触发）。
    // 这种情况再退避重连只会继续吃配额，所以直接放弃自动重连：把 m_aborted 置 true 让
    // onDisconnected 走 fast-return 分支，等用户下次 synthesize() 主动尝试时再说。
    if (errMsg.contains("403")) {
        qWarning() << "[TTS] HTTP 403 from speech.platform.bing.com — "
                      "停止自动重连。可能原因：网络 / 区域屏蔽，或刚才连续重连被 MS 临时限流。"
                      "等几分钟或换网络再试，或在 Settings → General 关掉 TTS。";
        m_aborted = true;
        m_consecutiveFailures = 0;
        m_clockSkewSec = 0;
    }

    m_synthesizing = false;
    m_ready = false;
    emit errorOccurred("WebSocket 错误: " + errMsg);
}
