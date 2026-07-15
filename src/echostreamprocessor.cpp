#include "echostreamprocessor.h"

#include <QtGlobal>

/** 追加字节并处理半包、粘包、原始块和校验失败。 */
EchoStreamProcessor::Result EchoStreamProcessor::appendData(const QByteArray &data)
{
    m_buffer.append(data);
    Result result;

    while (!m_buffer.isEmpty()) {
        const int headerIndex = findHeader(m_buffer);
        if (headerIndex < 0) {
            // 没有 A0 的数据是原始 Echo；尾部 A0 可能是拆开的帧头。
            const bool partialHeader = static_cast<quint8>(m_buffer.back()) == kHeaderByte;
            const int rawSize = m_buffer.size() - (partialHeader ? 1 : 0);
            if (rawSize > 0) {
                result.responses.push_back({m_buffer.left(rawSize), false});
                ++result.rawBlocks;
                m_buffer.remove(0, rawSize);
            }
            break;
        }

        if (headerIndex > 0) {
            result.responses.push_back({m_buffer.left(headerIndex), false});
            ++result.rawBlocks;
            m_buffer.remove(0, headerIndex);
            continue;
        }

        if (m_buffer.size() < 2) {
            break;
        }

        const int payloadSize = payloadLength(m_buffer);
        const int frameSize = payloadSize + kMinimumFrameSize;
        if (m_buffer.size() < frameSize) {
            // 半包：等待下一次 readyRead 补齐 N+3 个字节。
            break;
        }

        const QByteArray frame = m_buffer.left(frameSize);
        m_buffer.remove(0, frameSize);
        if (!checksumValid(frame)) {
            // 丢弃当前帧头并重新搜索，避免错误长度/校验导致永久失步。
            ++result.checksumErrors;
            continue;
        }

        result.responses.push_back({frame.mid(2, payloadSize), true});
        ++result.protocolFrames;
    }

    return result;
}

/** 返回当前半包缓存长度。 */
int EchoStreamProcessor::bufferedByteCount() const
{
    return m_buffer.size();
}

/** 清空当前客户端的半包缓存。 */
void EchoStreamProcessor::clear()
{
    m_buffer.clear();
}

/** 查找从指定位置开始的下一个 A0。 */
int EchoStreamProcessor::findHeader(const QByteArray &data, int from)
{
    for (int index = qMax(0, from); index < data.size(); ++index) {
        if (static_cast<quint8>(data.at(index)) == kHeaderByte) {
            return index;
        }
    }
    return -1;
}

/** 读取帧头后的单字节 payload 长度。 */
int EchoStreamProcessor::payloadLength(const QByteArray &data)
{
    return static_cast<quint8>(data.at(1));
}

/** 验证帧头、长度和 payload 的低八位累加和。 */
bool EchoStreamProcessor::checksumValid(const QByteArray &frame)
{
    if (frame.size() < kMinimumFrameSize) {
        return false;
    }

    quint8 checksum = 0;
    for (int index = 0; index + 1 < frame.size(); ++index) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(frame.at(index)));
    }
    return checksum == static_cast<quint8>(frame.back());
}
