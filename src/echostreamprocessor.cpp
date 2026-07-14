#include "echostreamprocessor.h"

#include <QtGlobal>

/**
 * @brief Appends TCP bytes and returns every complete echoable unit.
 *
 * The parser deliberately preserves the original bytes. It only determines
 * boundaries and records checksum/format errors; the server layer is expected
 * to echo the returned QByteArray values without changing their contents.
 */
EchoStreamProcessor::Result EchoStreamProcessor::appendData(const QByteArray &data)
{
    Result result;
    m_buffer.append(data);

    while (!m_buffer.isEmpty()) {
        // Locate the next A0 81 marker. Data before it is not part of a framed
        // packet and is returned as a raw block instead of being silently lost.
        const int headerIndex = findHeader(m_buffer);
        if (headerIndex < 0) {
            // Keep a trailing 0xA0 because it may be the first byte of a split protocol header.
            const bool hasPartialHeader = static_cast<quint8>(m_buffer.back()) == kHeaderByte;
            const int rawSize = m_buffer.size() - (hasPartialHeader ? 1 : 0);
            if (rawSize > 0) {
                result.responses.push_back(m_buffer.left(rawSize));
                ++result.rawBlocks;
                m_buffer.remove(0, rawSize);
            }
            break;
        }

        if (headerIndex > 0) {
            result.responses.push_back(m_buffer.left(headerIndex));
            ++result.rawBlocks;
            m_buffer.remove(0, headerIndex);
            continue;
        }

        if (m_buffer.size() < kMinimumFrameSize) {
            break;
        }

        // A complete minimum frame is available, so reading the four-byte length
        // field is safe. The upper limit prevents malformed input from creating
        // an unbounded wait or an unreasonable frame-size calculation.
        const quint32 payloadSize = payloadLength(m_buffer);
        if (payloadSize > kMaximumPayloadLength) {
            result.responses.push_back(m_buffer);
            ++result.rawBlocks;
            ++result.malformedFrames;
            m_buffer.clear();
            break;
        }

        // The protocol has an eight-byte header, payload, and one checksum byte.
        const qint64 frameSize = static_cast<qint64>(kHeaderSize) + payloadSize + 1;
        if (m_buffer.size() < frameSize) {
            break;
        }

        const QByteArray frame = m_buffer.left(static_cast<int>(frameSize));
        m_buffer.remove(0, static_cast<int>(frameSize));
        // An invalid checksum is reported but the frame is still echoed. This
        // lets a client observe exactly what arrived and keeps this component a
        // diagnostic echo server rather than a filtering gateway.
        if (!checksumValid(frame)) {
            ++result.checksumErrors;
        }
        result.responses.push_back(frame);
        ++result.protocolFrames;
    }

    return result;
}

int EchoStreamProcessor::bufferedByteCount() const
{
    return m_buffer.size();
}

void EchoStreamProcessor::clear()
{
    m_buffer.clear();
}

int EchoStreamProcessor::findHeader(const QByteArray &data)
{
    // Search only for the two-byte marker. The caller already handles a trailing
    // A0 so a marker split across two TCP reads is retained in m_buffer.
    for (int index = 0; index + 1 < data.size(); ++index) {
        if (static_cast<quint8>(data.at(index)) == kHeaderByte &&
            static_cast<quint8>(data.at(index + 1)) == kFrameTypeByte) {
            return index;
        }
    }
    return -1;
}

quint32 EchoStreamProcessor::payloadLength(const QByteArray &data)
{
    // The length is encoded most-significant byte first, independent of host
    // endianness. QByteArray::at() is safe because the caller checked the minimum
    // frame size before invoking this helper.
    quint32 length = 0;
    for (int offset = 0; offset < 4; ++offset) {
        length = (length << 8) | static_cast<quint8>(data.at(kLengthOffset + offset));
    }
    return length;
}

bool EchoStreamProcessor::checksumValid(const QByteArray &frame)
{
    // The checksum is the modulo-256 sum of every byte before the final byte.
    quint8 checksum = 0;
    for (int index = 0; index + 1 < frame.size(); ++index) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(frame.at(index)));
    }
    return checksum == static_cast<quint8>(frame.back());
}
