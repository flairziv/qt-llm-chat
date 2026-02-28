#pragma once
#include <QByteArray>
#include <QList>

/**
 * @brief 单个 SSE 事件的数据结构
 *
 * 对应 SSE 协议中的一个事件块，由空行分隔。
 * 一个典型的 SSE 事件块：
 *   event: content_block_delta
 *   data: {"type":"content_block_delta","delta":{"text":"Hello"}}
 *
 * 解析后：
 *   eventType = "content_block_delta"
 *   data = "{\"type\":\"content_block_delta\",\"delta\":{\"text\":\"Hello\"}}"
 */
struct SSEEvent {
    QByteArray eventType;   // 事件类型，如 "content_block_delta"、"message_stop"、"done"
    QByteArray data;        // 数据负载，通常是 JSON 字符串（"data:" 之后的内容）
};

/**
 * @brief Server-Sent Events (SSE) 流式解析器
 *
 * SSE 是一种基于 HTTP 的服务器推送协议，用于流式传输数据。
 * LLM API（Claude 和 OpenAI）都使用 SSE 协议进行流式响应，
 * 每生成一个 token 就通过 SSE 事件推送给客户端。
 *
 * SSE 协议格式：
 *   - 每个事件由一个或多个行组成
 *   - 行格式为 "field: value"（字段有 event、data、id、retry）
 *   - 事件之间用空行（\n\n 或 \r\n\r\n）分隔
 *   - 注释行以冒号开头
 *
 * 使用方式：
 *   SSEParser parser;
 *   // 每次从网络收到数据块时调用 feed()
 *   QList<SSEEvent> events = parser.feed(networkChunk);
 *   for (const SSEEvent &e : events) {
 *       // 处理完整的 SSE 事件
 *   }
 *
 * 设计要点：
 * - 内部维护缓冲区 m_buffer，处理 TCP 分包问题
 *   （一个 SSE 事件可能被拆分到多个 TCP 数据包中）
 * - 无状态解析：只关注 event: 和 data: 两个字段
 * - OpenAI 的结束标记 "data: [DONE]" 会被转换为 eventType="done"
 *
 * 调用链：
 *   QNetworkReply::readyRead
 *   → LLMProvider::onReadyRead()
 *   → SSEParser::feed(chunk) → 返回解析出的事件列表
 *   → LLMProvider::parseSSEData(event.data) → 提取 token 文本
 *   → emit tokenReceived(token)
 */
class SSEParser
{
public:
    SSEParser() = default;

    /**
     * @brief 输入原始网络字节，返回已完成的 SSE 事件列表
     *
     * 将新数据追加到内部缓冲区，然后尝试提取所有完整的事件块。
     * 不完整的事件会保留在缓冲区中，等待下次 feed() 补全。
     *
     * @param bytes 从 QNetworkReply::readAll() 获取的原始字节
     * @return 本次解析出的完整 SSE 事件列表（可能为空）
     */
    QList<SSEEvent> feed(const QByteArray &bytes);

    /**
     * @brief 重置解析器状态，清空内部缓冲区
     *
     * 在新的请求开始前调用，防止上次残留数据干扰。
     */
    void reset();

private:
    /**
     * @brief 解析单个 SSE 事件块
     *
     * 将一个由空行分隔的文本块解析为 SSEEvent 结构。
     * 逐行扫描，提取 "event:" 和 "data:" 字段。
     * 特殊处理：遇到 "data: [DONE]" 时设置 eventType="done"。
     *
     * @param block 一个完整的 SSE 事件块（不含尾部的空行分隔符）
     * @return 解析后的 SSEEvent，如果没有 data 字段则 data 为空
     */
    SSEEvent parseBlock(const QByteArray &block);

    QByteArray m_buffer;  // 内部缓冲区，累积未解析完的原始字节
};
