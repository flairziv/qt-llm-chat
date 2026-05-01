#include "SSEParser.h"

// ============================================================
// 核心方法 —— feed（输入数据，输出事件）
// ============================================================

/**
 * @brief 将原始网络字节输入解析器，提取已完成的 SSE 事件
 *
 * SSE 协议中，事件之间用空行分隔：
 *   - Unix 风格：  \n\n
 *   - Windows 风格：\r\n\r\n
 *
 * 由于 TCP 是流式协议，一次 readyRead 收到的数据可能包含：
 *   - 多个完整事件（会全部提取）
 *   - 一个事件被拆成两半（前半部分缓存，等下次 feed 补全）
 *   - 完整事件 + 不完整事件的前半部分
 *
 * 示例：
 *   第一次 feed 收到：
 *     "event: content_block_delta\ndata: {\"text\":\"Hel"
 *   → 没有找到 \n\n，全部缓存，返回空列表
 *
 *   第二次 feed 收到：
 *     "lo\"}\n\nevent: message_stop\ndata: {}\n\n"
 *   → 缓冲区拼接后找到两个 \n\n，返回两个事件
 *
 * @param bytes 从 QNetworkReply::readAll() 获取的原始字节
 * @return 本次解析出的完整 SSE 事件列表
 */
QList<SSEEvent> SSEParser::feed(const QByteArray &bytes)
{
    // 将新数据追加到缓冲区尾部
    m_buffer.append(bytes);
    QList<SSEEvent> events;

    // 循环提取所有完整的事件块（以双换行符分隔）
    // 关键优化：只前移读偏移 m_pos，不在每次事件后都做 m_buffer = m_buffer.mid(...)；
    // 否则单次 readyRead 收到 N 个事件、缓冲区有 M 字节时，开销是 O(N*M)。
    while (true) {
        // 优先查找 Unix 风格换行 \n\n
        int idx = m_buffer.indexOf("\n\n", m_pos);
        int sepLen = 2;
        if (idx == -1) {
            // 退而查找 Windows 风格换行 \r\n\r\n
            idx = m_buffer.indexOf("\r\n\r\n", m_pos);
            if (idx == -1) break;  // 没有完整事件，退出等待更多数据
            sepLen = 4;
        }

        // 提取分隔符之前的内容作为事件块，前移读偏移
        QByteArray block = m_buffer.mid(m_pos, idx - m_pos);
        m_pos = idx + sepLen;

        SSEEvent event = parseBlock(block);
        if (!event.data.isEmpty()) {
            events.append(event);
        }
    }

    // 周期性压缩缓冲区：
    // - 已消费部分超过 64KB 时回收（防止长会话下 m_buffer 无界增长）
    // - 缓冲区已被完全消费时也立刻回收，避免下一帧 indexOf 在脏数据上空跑
    if (m_pos >= 65536 || m_pos == m_buffer.size()) {
        m_buffer.remove(0, m_pos);
        m_pos = 0;
    }

    return events;
}

// ============================================================
// 重置
// ============================================================

/**
 * @brief 清空内部缓冲区
 *
 * 在 LLMProvider::sendStreamingRequest() 中，
 * 发送新请求前调用 reset()，确保上一次请求的残留数据不会影响新请求。
 */
void SSEParser::reset()
{
    m_buffer.clear();
    m_pos = 0;
}

// ============================================================
// 事件块解析
// ============================================================

/**
 * @brief 解析单个 SSE 事件块为 SSEEvent 结构
 *
 * 一个事件块由一个或多个行组成，每行格式为 "field: value"。
 * 本解析器只处理两种字段：
 *   - "event:" → 设置 eventType
 *   - "data:"  → 设置 data（多个 data 行会用 \n 连接）
 *
 * Claude API 的 SSE 示例：
 *   event: content_block_delta
 *   data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"你好"}}
 *   → eventType = "content_block_delta"
 *   → data = "{\"type\":\"content_block_delta\",...}"
 *
 * OpenAI API 的 SSE 示例：
 *   data: {"choices":[{"delta":{"content":"Hello"}}]}
 *   → eventType = ""（OpenAI 通常不使用 event 字段）
 *   → data = "{\"choices\":[...]}"
 *
 * OpenAI 结束标记的特殊处理：
 *   data: [DONE]
 *   → eventType = "done"，data = ""（清空）
 *   这样上层代码可以统一用 eventType == "done" 判断流结束
 *
 * @param block 一个完整的 SSE 事件块文本
 * @return 解析后的 SSEEvent
 */
SSEEvent SSEParser::parseBlock(const QByteArray &block)
{
    SSEEvent event;
    // 按换行符拆分为多行（兼容 \r\n，因为 trimmed() 会去掉 \r）
    QList<QByteArray> lines = block.split('\n');

    for (const QByteArray &rawLine : lines) {
        QByteArray line = rawLine.trimmed();

        if (line.startsWith("event:")) {
            // 提取 "event:" 之后的内容，去掉前后空白
            // 例如 "event: content_block_delta" → "content_block_delta"
            event.eventType = line.mid(6).trimmed();
        } else if (line.startsWith("data:")) {
            // 提取 "data:" 之后的内容
            QByteArray data = line.mid(5).trimmed();

            // OpenAI 的流结束标记：data: [DONE]
            // 不是有效 JSON，需要特殊处理
            if (data == "[DONE]") {
                event.data.clear();
                event.eventType = "done";
                return event;  // 立即返回，不再解析后续行
            }

            // SSE 规范允许一个事件包含多个 data 行，
            // 多个 data 行的内容用换行符连接
            // （实际上 LLM API 很少使用多行 data，但这里遵循规范）
            if (!event.data.isEmpty()) {
                event.data.append('\n');
            }
            event.data.append(data);
        }
        // 忽略其他字段（id:、retry:、注释行等）
    }
    return event;
}
