#include "echostreamprocessor.h"
#include "portrangeparser.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

QByteArray makeFrame(quint8 command, const QByteArray &payload = {})
{
    QByteArray frame;
    frame.append(static_cast<char>(0xA0));
    frame.append(static_cast<char>(0x81));
    frame.append(static_cast<char>(command));

    const quint32 size = static_cast<quint32>(payload.size());
    frame.append(static_cast<char>((size >> 24) & 0xFF));
    frame.append(static_cast<char>((size >> 16) & 0xFF));
    frame.append(static_cast<char>((size >> 8) & 0xFF));
    frame.append(static_cast<char>(size & 0xFF));
    frame.append(static_cast<char>(0x00));
    frame.append(payload);

    quint8 checksum = 0;
    for (char byte : frame) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    }
    frame.append(static_cast<char>(checksum));
    return frame;
}

bool check(bool condition, const char *message)
{
    if (!condition) {
        qCritical() << message;
    }
    return condition;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;

    const PortParseResult ports = PortRangeParser::parse(QStringLiteral("10162, 10160-10162, 10200"));
    passed &= check(ports.isValid(), "valid port list rejected");
    passed &= check(ports.ports == QVector<quint16>({10160, 10161, 10162, 10200}), "ports were not sorted or deduplicated");
    passed &= check(!PortRangeParser::parse(QStringLiteral("0,10160")).isValid(), "zero port accepted");
    passed &= check(!PortRangeParser::parse(QStringLiteral("10170-10160")).isValid(), "reversed range accepted");
    passed &= check(!PortRangeParser::parse(QStringLiteral("1-10"), 5).isValid(), "port limit ignored");

    const QByteArray firstFrame = makeFrame(0x01);
    const QByteArray secondFrame = makeFrame(0x02, QByteArray::fromHex("0102030405"));

    EchoStreamProcessor splitProcessor;
    passed &= check(splitProcessor.appendData(firstFrame.left(4)).responses.isEmpty(), "partial frame emitted early");
    const auto completed = splitProcessor.appendData(firstFrame.mid(4));
    passed &= check(completed.responses == QVector<QByteArray>({firstFrame}), "split frame not reassembled");
    passed &= check(completed.protocolFrames == 1, "protocol frame counter incorrect");

    EchoStreamProcessor stickyProcessor;
    const auto sticky = stickyProcessor.appendData(firstFrame + secondFrame);
    passed &= check(sticky.responses == QVector<QByteArray>({firstFrame, secondFrame}), "sticky frames not separated");

    EchoStreamProcessor rawProcessor;
    const QByteArray raw("ASCII command\r\n");
    const auto rawResult = rawProcessor.appendData(raw);
    passed &= check(rawResult.responses == QVector<QByteArray>({raw}), "raw block not echoed");
    passed &= check(rawResult.rawBlocks == 1, "raw block counter incorrect");

    EchoStreamProcessor mixedProcessor;
    const QByteArray prefix("raw");
    const auto mixed = mixedProcessor.appendData(prefix + secondFrame);
    passed &= check(mixed.responses == QVector<QByteArray>({prefix, secondFrame}), "raw prefix and protocol frame not separated");

    QByteArray invalidChecksum = firstFrame;
    invalidChecksum[invalidChecksum.size() - 1] ^= 0x01;
    EchoStreamProcessor checksumProcessor;
    const auto checksumResult = checksumProcessor.appendData(invalidChecksum);
    passed &= check(checksumResult.responses == QVector<QByteArray>({invalidChecksum}), "invalid frame was not echoed unchanged");
    passed &= check(checksumResult.checksumErrors == 1, "checksum error not counted");

    if (!passed) {
        return 1;
    }
    qInfo() << "SerialServer core tests passed";
    return 0;
}
