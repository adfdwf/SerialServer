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

/**
 * @brief Builds a valid protocol frame for the integration test client.
 */
QByteArray makeFrame(const QByteArray &payload)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xA0));
    frame.append(static_cast<char>(payload.size()));
    frame.append(payload);

    quint8 checksum = 0;
    for (char byte : frame) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    }
    frame.append(static_cast<char>(checksum));
    return frame;
}

/**
 * @brief Reads until a requested number of bytes arrives or the timeout expires.
 *
 * The helper mirrors real TCP behavior: one waitForReadyRead call is not
 * assumed to return a complete application frame.
 */
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

/**
 * @brief Sends one request and verifies the device-style response.
 */
bool expectProtocolResponse(QTcpSocket &socket,
                            const QByteArray &request,
                            const QByteArray &expected,
                            const char *message)
{
    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(3000)) {
        qCritical() << message << "write failed:" << socket.errorString();
        return false;
    }
    const QByteArray response = readExactly(socket, expected.size(), 3000);
    if (response != expected) {
        qCritical() << message << "expected" << expected.toHex() << "got" << response.toHex();
        return false;
    }
    return true;
}

} // namespace

/**
 * @brief Runs the worker in a QThread and exercises its TCP echo behavior.
 */
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    qRegisterMetaType<PortStats>("PortStats");

    // Reserve an available ephemeral port first so the test does not depend on
    // a hard-coded port being free on the developer's machine.
    QTcpServer portProbe;
    if (!portProbe.listen(QHostAddress::LocalHost, 0)) {
        qCritical() << "Cannot reserve a test port:" << portProbe.errorString();
        return 1;
    }
    const quint16 port = portProbe.serverPort();
    portProbe.close();

    // The worker is moved to a dedicated thread exactly as the GUI does it.
    QThread serverThread;
    auto *worker = new PortServerWorker(QHostAddress::LocalHost, port);
    worker->moveToThread(&serverThread);
    QObject::connect(&serverThread, &QThread::started, worker, &PortServerWorker::start);
    QObject::connect(worker, &PortServerWorker::stopped, &serverThread, &QThread::quit, Qt::DirectConnection);
    QObject::connect(&serverThread, &QThread::finished, worker, &QObject::deleteLater);

    // Wait asynchronously for the worker to publish Listening or Failed. The
    // timeout keeps a broken startup from hanging the test process forever.
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
            // Verify that a frame split across two TCP writes is reassembled.
            const QByteArray splitPayload = QByteArray::fromHex("010203");
            const QByteArray splitFrame = makeFrame(splitPayload);
            const QByteArray firstPart = splitFrame.left(4);
            const QByteArray secondPart = splitFrame.mid(4);
            passed &= socket.write(firstPart) == firstPart.size();
            passed &= socket.waitForBytesWritten(3000);
            passed &= !socket.waitForReadyRead(100);
            passed &= socket.write(secondPart) == secondPart.size();
            passed &= socket.waitForBytesWritten(3000);
            const QByteArray splitResponse = readExactly(socket, splitPayload.size(), 3000);
            if (splitResponse != splitPayload) {
                qCritical() << "split frame expected" << splitPayload.toHex() << "got" << splitResponse.toHex();
                passed = false;
            }

            // Verify that two protocol frames are transformed independently and
            // raw ASCII remains an unchanged response block.
            const QByteArray firstSticky = makeFrame(QByteArray("first"));
            const QByteArray secondSticky = makeFrame(QByteArray("payload"));
            passed &= expectProtocolResponse(socket,
                                             firstSticky + secondSticky,
                                             QByteArray("firstpayload"),
                                             "sticky frames");
            const QByteArray raw = QByteArray("ASCII command\r\n");
            passed &= expectProtocolResponse(socket, raw, raw, "raw ASCII");
            socket.disconnectFromHost();
            socket.waitForDisconnected(1000);
        }
    }

    // Always stop the worker, including after a failed assertion, so the test
    // does not leave a QThread running during process shutdown.
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
