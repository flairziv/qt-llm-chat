#pragma once

#include <QObject>
#include <QWebSocket>

/**
 * @brief 基于微软 Edge 浏览器"大声朗读"功能的 TTS（文本转语音）服务提供者。
 *
 * 通过 WebSocket 连接 Bing 语音合成服务，将文本转换为 PCM 音频流。
 * 算法与连接参数逆向自 edge-tts Python 项目 (https://github.com/rany2/edge-tts)。
 *
 * 支持预连接（preConnect）和连接复用：
 *   - 程序启动时调用 preConnect() 提前建立 WebSocket 连接
 *   - synthesize() 时若连接已就绪则直接发送 SSML，跳过建连延迟
 *   - 合成完成后保持连接，下一次 synthesize() 可复用
 */
class EdgeTTSProvider : public QObject
{
    Q_OBJECT
public:
    explicit EdgeTTSProvider(QObject *parent = nullptr);
    ~EdgeTTSProvider() override;

    /**
     * @brief 预建立 WebSocket 连接。
     *
     * 程序启动或 abort 后调用，提前完成建连 + 发送 config，
     * 使后续 synthesize() 可以零延迟发送 SSML。
     */
    void preConnect();

    /**
     * @brief 发起语音合成请求。
     * @param text       要转换的文本内容
     * @param voiceName  语音名称，如 "zh-CN-XiaoxiaoNeural"
     *
     * 若连接已就绪（m_ready），直接发送 SSML（零延迟）。
     * 若未连接，先建连，连接成功后自动发送。
     */
    void synthesize(const QString &text, const QString &voiceName);

    /** @brief 中止当前正在进行的合成，关闭 WebSocket 连接。 */
    void abort();

signals:
    /** @brief WebSocket 连接就绪，可直接调用 synthesize。 */
    void connectionReady();

    /** @brief 合成流程开始（SSML 已发送）。 */
    void synthesisStarted();

    /** @brief 收到一块 PCM 音频数据，可立即推送给 QAudioOutput 播放。 */
    void audioChunkReceived(const QByteArray &pcmData);

    /** @brief 合成完成（所有音频块已发送），返回临时 MP3 文件路径。 */
    void synthesisFinished(const QString &audioFilePath);

    /** @brief 合成过程中发生错误。@param errorMessage 错误描述 */
    void errorOccurred(const QString &errorMessage);

private slots:
    void onConnected();                                     // WebSocket 连接成功回调
    void onTextMessageReceived(const QString &message);     // 接收服务端文本消息（用于检测 turn.end）
    void onBinaryMessageReceived(const QByteArray &message);// 接收服务端二进制音频数据
    void onDisconnected();                                  // 连接断开回调
    void onWsError(QAbstractSocket::SocketError error);     // 连接错误回调

private:
    void connectWs();                                       // 创建 WebSocket 并发起连接
    void sendConfig();                                      // 发送音频输出格式等配置
    void sendSSML(const QString &text, const QString &voiceName); // 发送 SSML 合成请求

    QString generateSecMsGec();          // 用 m_clockSkewSec 修正后的时间生成 DRM 令牌
    static QString generateUUID();       // 生成无连字符的 UUID，用作 ConnectionId / RequestId
    static QString xmlEscape(const QString &s); // XML 特殊字符转义
    static QString generateMuid();       // 生成随机 MUID Cookie 值
    static QString dateToString();       // JS-style date 字符串，用于 X-Timestamp 头

    QWebSocket *m_ws = nullptr;          // WebSocket 连接实例
    QString     m_requestId;             // 当前合成请求的唯一标识
    QString     m_pendingText;           // 待合成的文本（连接建立前暂存）
    QString     m_pendingVoice;          // 待使用的语音名称（连接建立前暂存）
    QByteArray  m_audioBuffer;           // 累积接收的 MP3 音频数据
    bool        m_synthesizing = false;  // 是否正在进行合成
    bool        m_ready = false;         // 连接已建立且 config 已发送，可直接发 SSML
    bool        m_aborted = false;       // 是否被显式中止（阻止自动重连)

    // ---- 失败重连：避免在 RemoteHostClosed / 403 上死循环 ----
    // 自动重连最多 2 次：先把 m_clockSkewSec 挪 -300（防客户端时钟刚跨过 5 分钟
    // 边界），再回到 0。再失败就停止——继续重连只会让 MS 把 IP 拉黑。一次握手
    // 成功后 m_consecutiveFailures 清零。HTTP 403 直接放弃（onWsError 里处理）。
    qint64 m_clockSkewSec = 0;
    int m_consecutiveFailures = 0;
};
