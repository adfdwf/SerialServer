#include "portserverworker.h"

#include <QAbstractSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace {
constexpr qint64 kMaximumQueuedWriteBytes = 64LL * 1024 * 1024;
constexpr qint64 kSocketReadBufferBytes = 16LL * 1024 * 1024;
}

PortServerWorker::PortServerWorker(QHostAddress address, quint16 port, QObject *parent)
    : QObject(parent), m_address(std::move(address)), m_port(port)
{
    m_stats.port = port;
    m_stats.state = QStringLiteral("Starting");
}

void PortServerWorker::start()
{
    if (m_started) {
        return;
    }
    m_started = true;

    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(4096);
    QObject::connect(m_server, &QTcpServer::newConnection, this, &PortServerWorker::acceptConnections);
    QObject::connect(m_server, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        ++m_stats.protocolErrors;
        m_stats.lastError = m_server->errorString();
        publishStats();
    });

    if (!m_server->listen(m_address, m_port)) {
        m_stats.state = QStringLiteral("Failed");
        m_stats.lastError = m_server->errorString();
        publishStats();
        emit stopped(m_port);
        return;
    }

    m_stats.state = QStringLiteral("Listening");
    m_rateTimer.start();
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(500);
    QObject::connect(m_statsTimer, &QTimer::timeout, this, &PortServerWorker::publishStats);
    m_statsTimer->start();
    publishStats();
}

void PortServerWorker::stop()
{
    if (m_stopping) {
        return;
    }
    m_stopping = true;

    if (m_statsTimer) {
        m_statsTimer->stop();
    }
    if (m_server) {
        m_server->close();
    }

    const QList<QTcpSocket *> sockets = m_processors.keys();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_processors.clear();
    m_stats.activeConnections = 0;
    m_stats.state = QStringLiteral("Stopped");
    publishStats();
    emit stopped(m_port);
}

void PortServerWorker::acceptConnections()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        socket->setReadBufferSize(kSocketReadBufferBytes);
        m_processors.insert(socket, EchoStreamProcessor());
        ++m_stats.activeConnections;
        ++m_stats.acceptedConnections;

        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleReadyRead(socket);
        });
        QObject::connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            removeConnection(socket);
        });
        QObject::connect(socket,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                         &QTcpSocket::errorOccurred,
#else
                         QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
#endif
                         this,
                         [this, socket](QAbstractSocket::SocketError error) {
                             if (error == QAbstractSocket::RemoteHostClosedError) {
                                 return;
                             }
                             ++m_stats.protocolErrors;
                             m_stats.lastError = socket->errorString();
                         });
    }
}

void PortServerWorker::handleReadyRead(QTcpSocket *socket)
{
    auto processor = m_processors.find(socket);
    if (processor == m_processors.end()) {
        socket->readAll();
        return;
    }

    const QByteArray data = socket->readAll();
    m_stats.receivedBytes += static_cast<quint64>(data.size());
    const EchoStreamProcessor::Result result = processor->appendData(data);
    m_stats.receivedRequests += static_cast<quint64>(result.responses.size());
    m_stats.protocolErrors += result.checksumErrors + result.malformedFrames;

    for (const QByteArray &response : result.responses) {
        if (socket->bytesToWrite() > kMaximumQueuedWriteBytes) {
            ++m_stats.protocolErrors;
            m_stats.lastError = QStringLiteral("Connection closed because its write queue exceeded 64 MiB");
            socket->abort();
            return;
        }

        const qint64 queuedBytes = socket->write(response);
        if (queuedBytes < 0) {
            ++m_stats.protocolErrors;
            m_stats.lastError = socket->errorString();
            continue;
        }
        m_stats.sentBytes += static_cast<quint64>(queuedBytes);
        ++m_stats.sentResponses;
    }
}

void PortServerWorker::removeConnection(QTcpSocket *socket)
{
    const auto processor = m_processors.find(socket);
    if (processor != m_processors.end()) {
        if (processor->bufferedByteCount() > 0) {
            ++m_stats.protocolErrors;
            m_stats.lastError = QStringLiteral("Connection closed with an incomplete request");
        }
        m_processors.erase(processor);
        if (m_stats.activeConnections > 0) {
            --m_stats.activeConnections;
        }
    }
    socket->deleteLater();
}

void PortServerWorker::publishStats()
{
    if (m_rateTimer.isValid()) {
        const qint64 elapsedMs = m_rateTimer.restart();
        if (elapsedMs > 0) {
            m_stats.receiveBytesPerSecond =
                static_cast<double>(m_stats.receivedBytes - m_previousReceivedBytes) * 1000.0 / elapsedMs;
            m_stats.sendBytesPerSecond =
                static_cast<double>(m_stats.sentBytes - m_previousSentBytes) * 1000.0 / elapsedMs;
            m_previousReceivedBytes = m_stats.receivedBytes;
            m_previousSentBytes = m_stats.sentBytes;
        }
    }
    emit statsUpdated(m_stats);
}
