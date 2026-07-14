#ifndef ECHOSTREAMPROCESSOR_H
#define ECHOSTREAMPROCESSOR_H

#include <QByteArray>
#include <QVector>

class EchoStreamProcessor final
{
public:
    struct Result {
        QVector<QByteArray> responses;
        quint64 protocolFrames = 0;
        quint64 rawBlocks = 0;
        quint64 checksumErrors = 0;
        quint64 malformedFrames = 0;
    };

    Result appendData(const QByteArray &data);
    int bufferedByteCount() const;
    void clear();

private:
    static constexpr quint8 kHeaderByte = 0xA0;
    static constexpr quint8 kFrameTypeByte = 0x81;
    static constexpr int kLengthOffset = 3;
    static constexpr int kHeaderSize = 8;
    static constexpr int kMinimumFrameSize = 9;
    static constexpr quint32 kMaximumPayloadLength = 1024 * 1024;

    static int findHeader(const QByteArray &data);
    static quint32 payloadLength(const QByteArray &data);
    static bool checksumValid(const QByteArray &frame);

    QByteArray m_buffer;
};

#endif // ECHOSTREAMPROCESSOR_H
