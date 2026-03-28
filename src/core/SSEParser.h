#pragma once
#include <QByteArray>
#include <QList>

struct SSEEvent {
    QByteArray eventType;
    QByteArray data;
};

class SSEParser
{
public:
    SSEParser() = default;
    QList<SSEEvent> feed(const QByteArray &bytes);
    void reset();

private:
    SSEEvent parseBlock(const QByteArray &block);
    QByteArray m_buffer;
};
