#include "portserverworker.h"

#include <QAbstractSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace {
// A client that never reads can otherwise make Qt accumulate an unlimited
// amount of pending echo data. The limit protects the worker and the process.
constexpr qint64 kMaximumQueuedWriteBytes = 64LL * 1024 * 1024;

// This read-buffer size is large enough for bursty TCP clients while still
// providing a bounded per-socket memory cost.
constexpr qint64 kSocketReadBufferBytes = 16LL * 1024 * 1024;
}

/**
 * @brief Initializes the endpoint and its initial statistics state.
 */
PortServerWorker::PortServerWorker(QHostAddress address, quint16 port, QObject *parent)
    : QObject(parent), m_address(std::move(address)), m_port(port)
{
    m_stats.port = port;
    m_stats.state = QStringLiteral("Starting");
}

/**
 * @brief Creates the listening socket and starts periodic statistics updates.
 *
 * The method is invoked through QThread::started, so all QTcpServer and QTimer
 * objects are created in the worker thread that owns them.
 */
void PortServerWorker::start()
{
    if (m_started) {
        return;
    }
    m_started = true;

    // One QTcpServer represents one configured port. The pending-connection
    // queue is deliberately larger than the number of active worker threads.
    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(4096);
    QObject::connect(m_server, &QTcpServer::newConnection, this, &PortServerWorker::acceptConnections);
    QObject::connect(m_server, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        ++m_stats.protocolErrors;
        m_stats.lastError = m_server->errorString();
        publishStats();
    });

    // listen() binds the local interface. This server does not connect to a
    // remote serial server; clients such as NetAssist connect to this endpoint.
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

/**
 * @brief Stops timers, closes the listening socket, and aborts active clients.
 */
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

    // Copy the keys before deleting sockets because abort/disconnect signals may
    // modify the original hash while the shutdown is in progress.
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

/**
 * @brief Accepts all currently pending clients and connects their signals.
 */
void PortServerWorker::acceptConnections()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        // LowDelayOption maps to TCP_NODELAY and avoids adding Nagle delay to
        // small request/response exchanges used by the benchmark.
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

/**
 * @brief Reads, frames, validates, and echoes data from one client.
 *
 * readAll() may contain partial or multiple application frames. The per-socket
 * EchoStreamProcessor owns that framing state, so clients cannot interfere with
 * one another's incomplete packets.
 */
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

    // write() only queues bytes in Qt; bytesToWrite() is checked before each
    // response to prevent an unresponsive client from exhausting memory.
    for (const QByteArray &response : result.responses) {
        if (socket->bytesToWrite() > kMaximumQueuedWriteBytes) {
            ++m_stats.protocolErrors;
            m_stats.lastError = QStringLiteral("Connection closed because its write queue exceeded 64 MiB");
            socket->abort();
            return;
        }

        const QByteArray deviceResponse = EchoStreamProcessor::buildProtocolResponse(response);
        const qint64 queuedBytes = socket->write(deviceResponse);
        if (queuedBytes < 0) {
            ++m_stats.protocolErrors;
            m_stats.lastError = socket->errorString();
            continue;
        }
        m_stats.sentBytes += static_cast<quint64>(queuedBytes);
        ++m_stats.sentResponses;
    }
}

/**
 * @brief Removes a disconnected client and reports an incomplete tail.
 */
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

/**
 * @brief Calculates interval rates and emits a copy of the current counters.
 */
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
