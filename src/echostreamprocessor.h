#ifndef ECHOSTREAMPROCESSOR_H
#define ECHOSTREAMPROCESSOR_H

#include <QByteArray>
#include <QVector>

/**
 * @brief 按 A0 + 长度 + payload + 校验和格式解析一个 TCP 字节流。
 *
 * 解析器保存每个客户端自己的半包缓存，能够处理半包、粘包、原始数据和
 * 校验失败。合法协议帧输出去掉帧头、长度和校验字节后的 payload；无协议头
 * 数据保持原样输出；校验失败的帧被丢弃并重新搜索后续 A0。
 */
class EchoStreamProcessor final
{
public:
    /** 一次 appendData 调用产生的响应单元。 */
    struct Response {
        QByteArray data;            ///< 要写回客户端的数据，协议帧时为 payload。
        bool protocolFrame = false; ///< true 表示 data 是合法协议帧的 payload。
    };

    /** 一次解析调用的响应和统计结果。 */
    struct Result {
        QVector<Response> responses; ///< 合法 payload 或无协议头原始数据块。
        quint64 protocolFrames = 0;  ///< 本次成功解析的协议帧数量。
        quint64 rawBlocks = 0;       ///< 本次输出的原始数据块数量。
        quint64 checksumErrors = 0;  ///< 本次丢弃的校验失败帧数量。
        quint64 malformedFrames = 0; ///< 保留字段，表示长度/结构异常。
    };

    /** @brief 追加 TCP 字节并提取当前所有完整响应单元。 */
    Result appendData(const QByteArray &data);

    /** @brief 返回半包缓存字节数。 */
    int bufferedByteCount() const;

    /** @brief 清空半包缓存。 */
    void clear();

private:
    static constexpr quint8 kHeaderByte = 0xA0; ///< 固定帧头。
    static constexpr int kMinimumFrameSize = 3; ///< A0、长度和校验和。

    /** @brief 查找下一个 A0 帧头。 */
    static int findHeader(const QByteArray &data, int from = 0);

    /** @brief 读取帧头后的一个字节长度 N。 */
    static int payloadLength(const QByteArray &data);

    /** @brief 验证帧累加和。 */
    static bool checksumValid(const QByteArray &frame);

    QByteArray m_buffer; ///< 当前客户端尚未完成解析的字节缓存。
};

#endif // ECHOSTREAMPROCESSOR_H
