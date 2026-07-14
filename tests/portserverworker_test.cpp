#include "portserverworker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

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

QByteArray readExactly(QTcpSocket &socket, int expectedBytes, int timeoutMs)
{
    QByteArray received;
    QElapsedTimer timer;
    timer.start();
    while (received.size() < expectedBytes && timer.elapsed() < timeoutMs) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(qMax(1, timeoutMs - static_cast<int>(timer.elapsed())))) {
            break;
        }
        received.append(socket.readAll());
    }
    return received;
}

bool expectEcho(QTcpSocket &socket, const QByteArray &request, const char *message)
{
    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(3000)) {
        qCritical() << message << "write failed:" << socket.errorString();
        return false;
    }
    const QByteArray response = readExactly(socket, request.size(), 3000);
    if (response != request) {
        qCritical() << message << "expected" << request.toHex() << "got" << response.toHex();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    qRegisterMetaType<PortStats>("PortStats");

    QTcpServer portProbe;
    if (!portProbe.listen(QHostAddress::LocalHost, 0)) {
        qCritical() << "Cannot reserve a test port:" << portProbe.errorString();
        return 1;
    }
    const quint16 port = portProbe.serverPort();
    portProbe.close();

    QThread serverThread;
    auto *worker = new PortServerWorker(QHostAddress::LocalHost, port);
    worker->moveToThread(&serverThread);
    QObject::connect(&serverThread, &QThread::started, worker, &PortServerWorker::start);
    QObject::connect(worker, &PortServerWorker::stopped, &serverThread, &QThread::quit, Qt::DirectConnection);
    QObject::connect(&serverThread, &QThread::finished, worker, &QObject::deleteLater);

    bool listening = false;
    QString startupError;
    QEventLoop startupLoop;
    QTimer startupTimeout;
    startupTimeout.setSingleShot(true);
    QObject::connect(&startupTimeout, &QTimer::timeout, &startupLoop, &QEventLoop::quit);
    QObject::connect(worker, &PortServerWorker::statsUpdated, &startupLoop, [&](const PortStats &stats) {
        if (stats.state == QStringLiteral("Listening")) {
            listening = true;
            startupLoop.quit();
        } else if (stats.state == QStringLiteral("Failed")) {
            startupError = stats.lastError;
            startupLoop.quit();
        }
    });
    serverThread.start();
    startupTimeout.start(5000);
    startupLoop.exec();

    bool passed = listening;
    if (!listening) {
        qCritical() << "Server did not start:" << startupError;
    } else {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        passed &= socket.waitForConnected(3000);
        if (!passed) {
            qCritical() << "Client connection failed:" << socket.errorString();
        } else {
            const QByteArray splitFrame = makeFrame(0x01, QByteArray::fromHex("010203040506"));
            const QByteArray firstPart = splitFrame.left(4);
            const QByteArray secondPart = splitFrame.mid(4);
            passed &= socket.write(firstPart) == firstPart.size();
            passed &= socket.waitForBytesWritten(3000);
            passed &= !socket.waitForReadyRead(100);
            passed &= socket.write(secondPart) == secondPart.size();
            passed &= socket.waitForBytesWritten(3000);
            const QByteArray splitResponse = readExactly(socket, splitFrame.size(), 3000);
            if (splitResponse != splitFrame) {
                qCritical() << "split frame expected" << splitFrame.toHex() << "got" << splitResponse.toHex();
                passed = false;
            }

            const QByteArray stickyFrames = makeFrame(0x02) + makeFrame(0x03, QByteArray("payload"));
            passed &= expectEcho(socket, stickyFrames, "sticky frames");
            passed &= expectEcho(socket, QByteArray("ASCII command\r\n"), "raw ASCII");
            socket.disconnectFromHost();
            socket.waitForDisconnected(1000);
        }
    }

    if (serverThread.isRunning()) {
        QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait(5000);
    }

    if (!passed) {
        return 1;
    }
    qInfo() << "PortServerWorker integration test passed";
    return 0;
}
