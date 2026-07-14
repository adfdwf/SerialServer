#ifndef ECHOSTREAMPROCESSOR_H
#define ECHOSTREAMPROCESSOR_H

#include <QByteArray>
#include <QVector>

/**
 * @brief Splits a TCP byte stream into protocol frames and raw blocks.
 *
 * TCP does not preserve application message boundaries. One readyRead signal
 * may contain half a frame, one frame, or several frames. This class keeps the
 * incomplete tail in m_buffer and returns only complete units to the caller.
 * Complete units are returned unchanged by the splitter; the server layer then
 * converts A0 81 request frames to the device-style response format.
 */
class EchoStreamProcessor final
{
public:
    /** Builds the device-style response shown by the client protocol example. */
    static QByteArray buildProtocolResponse(const QByteArray &request);

    /**
     * @brief Describes the complete units extracted during one appendData call.
     *
     * responses contains both recognized protocol frames and raw byte blocks.
     * The counters are intentionally separate so the UI can distinguish normal
     * protocol traffic from unframed data and malformed input.
     */
    struct Result {
        QVector<QByteArray> responses;
        quint64 protocolFrames = 0;
        quint64 rawBlocks = 0;
        quint64 checksumErrors = 0;
        quint64 malformedFrames = 0;
    };

    /**
     * @brief Appends bytes and extracts every complete frame currently available.
     * @param data Newly received TCP bytes.
     * @return Extracted responses and parsing counters for this call.
     */
    Result appendData(const QByteArray &data);

    /**
     * @brief Returns the number of bytes waiting for a future TCP read.
     * @return Number of buffered incomplete bytes.
     */
    int bufferedByteCount() const;

    /**
     * @brief Discards an incomplete buffered unit.
     */
    void clear();

private:
    // Protocol frames start with A0 81. The four-byte length field is big-endian
    // and starts at byte offset 3. Byte 7 is reserved/header data; byte 8 onward
    // is payload, followed by one checksum byte.
    static constexpr quint8 kHeaderByte = 0xA0;
    static constexpr quint8 kFrameTypeByte = 0x81;
    static constexpr int kLengthOffset = 3;
    static constexpr int kHeaderSize = 8;
    static constexpr int kMinimumFrameSize = 9;
    static constexpr quint32 kMaximumPayloadLength = 4 * 1024 * 1024;

    static int findHeader(const QByteArray &data);
    static quint32 payloadLength(const QByteArray &data);
    static bool checksumValid(const QByteArray &frame);

    QByteArray m_buffer;
};

#endif // ECHOSTREAMPROCESSOR_H
