#include "echostreamprocessor.h"
#include "portrangeparser.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

/** 构造 A0 + 单字节长度 + payload + 校验和协议帧。 */
QByteArray makeFrame(const QByteArray &payload)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xA0));
    frame.append(static_cast<char>(payload.size()));
    frame.append(payload);
    quint8 checksum = 0;
    for (char byte : frame) checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    frame.append(static_cast<char>(checksum));
    return frame;
}

/** 记录失败断言并返回断言结果。 */
bool check(bool condition, const char *message)
{
    if (!condition) qCritical() << message;
    return condition;
}

} // namespace

/** 执行端口列表和新 TCP 协议流解析测试。 */
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;

    const PortParseResult ports = PortRangeParser::parse(QStringLiteral("10162, 10160-10162, 10200"));
    passed &= check(ports.isValid(), "valid port list rejected");
    passed &= check(ports.ports == QVector<quint16>({10160, 10161, 10162, 10200}), "ports were not sorted or deduplicated");

    const QByteArray firstPayload = QByteArray::fromHex("010203");
    const QByteArray secondPayload = QByteArray("payload");
    const QByteArray firstFrame = makeFrame(firstPayload);
    const QByteArray secondFrame = makeFrame(secondPayload);

    EchoStreamProcessor splitProcessor;
    passed &= check(splitProcessor.appendData(firstFrame.left(2)).responses.isEmpty(), "partial frame emitted early");
    const auto completed = splitProcessor.appendData(firstFrame.mid(2));
    passed &= check(completed.responses.size() == 1 && completed.responses[0].data == firstPayload && completed.responses[0].protocolFrame, "split frame payload incorrect");
    passed &= check(completed.protocolFrames == 1, "protocol frame counter incorrect");

    EchoStreamProcessor stickyProcessor;
    const auto sticky = stickyProcessor.appendData(firstFrame + secondFrame);
    passed &= check(sticky.responses.size() == 2 && sticky.responses[0].data == firstPayload && sticky.responses[1].data == secondPayload, "sticky frames not separated");

    EchoStreamProcessor rawProcessor;
    const QByteArray raw("ASCII command\r\n");
    const auto rawResult = rawProcessor.appendData(raw);
    passed &= check(rawResult.responses.size() == 1 && rawResult.responses[0].data == raw && !rawResult.responses[0].protocolFrame, "raw block not echoed");

    EchoStreamProcessor mixedProcessor;
    const auto mixed = mixedProcessor.appendData(QByteArray("raw") + firstFrame);
    passed &= check(mixed.responses.size() == 2 && mixed.responses[0].data == QByteArray("raw") && mixed.responses[1].data == firstPayload, "raw prefix and payload not separated");

    QByteArray invalidFrame = firstFrame;
    invalidFrame[invalidFrame.size() - 1] ^= 0x01;
    EchoStreamProcessor invalidProcessor;
    const auto invalid = invalidProcessor.appendData(invalidFrame + secondFrame);
    passed &= check(invalid.responses.size() == 1 && invalid.responses[0].data == secondPayload, "invalid frame was not discarded and resynchronized");
    passed &= check(invalid.checksumErrors == 1, "checksum error not counted");

    if (!passed) return 1;
    qInfo() << "SerialServer core tests passed";
    return 0;
}
