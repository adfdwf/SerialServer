#include "echostreamprocessor.h"

#include <QtGlobal>

EchoStreamProcessor::Result EchoStreamProcessor::appendData(const QByteArray &data)
{
    Result result;
    m_buffer.append(data);

    while (!m_buffer.isEmpty()) {
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

        const quint32 payloadSize = payloadLength(m_buffer);
        if (payloadSize > kMaximumPayloadLength) {
            result.responses.push_back(m_buffer);
            ++result.rawBlocks;
            ++result.malformedFrames;
            m_buffer.clear();
            break;
        }

        const qint64 frameSize = static_cast<qint64>(kHeaderSize) + payloadSize + 1;
        if (m_buffer.size() < frameSize) {
            break;
        }

        const QByteArray frame = m_buffer.left(static_cast<int>(frameSize));
        m_buffer.remove(0, static_cast<int>(frameSize));
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
    quint32 length = 0;
    for (int offset = 0; offset < 4; ++offset) {
        length = (length << 8) | static_cast<quint8>(data.at(kLengthOffset + offset));
    }
    return length;
}

bool EchoStreamProcessor::checksumValid(const QByteArray &frame)
{
    quint8 checksum = 0;
    for (int index = 0; index + 1 < frame.size(); ++index) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(frame.at(index)));
    }
    return checksum == static_cast<quint8>(frame.back());
}
