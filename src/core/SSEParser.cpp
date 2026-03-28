#include "SSEParser.h"

QList<SSEEvent> SSEParser::feed(const QByteArray &bytes)
{
    m_buffer.append(bytes);
    QList<SSEEvent> events;
    while (true) {
        int idx = m_buffer.indexOf("\n\n");
        if (idx == -1) {
            idx = m_buffer.indexOf("\r\n\r\n");
            if (idx == -1) break;
            QByteArray block = m_buffer.left(idx);
            m_buffer = m_buffer.mid(idx + 4);
            SSEEvent event = parseBlock(block);
            if (!event.data.isEmpty()) events.append(event);
            continue;
        }
        QByteArray block = m_buffer.left(idx);
        m_buffer = m_buffer.mid(idx + 2);
        SSEEvent event = parseBlock(block);
        if (!event.data.isEmpty()) events.append(event);
    }
    return events;
}

void SSEParser::reset()
{
    m_buffer.clear();
}

SSEEvent SSEParser::parseBlock(const QByteArray &block)
{
    SSEEvent event;
    const QList<QByteArray> lines = block.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.startsWith("event:")) {
            event.eventType = line.mid(6).trimmed();
        } else if (line.startsWith("data:")) {
            QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]") {
                event.eventType = "done";
                event.data.clear();
            } else {
                if (!event.data.isEmpty()) event.data += '\n';
                event.data += data;
            }
        }
    }
    return event;
}
