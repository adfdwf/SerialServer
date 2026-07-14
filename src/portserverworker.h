#ifndef PORTSERVERWORKER_H
#define PORTSERVERWORKER_H

#include "echostreamprocessor.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QtGlobal>

class QTcpServer;
class QTcpSocket;
class QTimer;

/**
 * @brief Snapshot of one listening port's runtime state and counters.
 *
 * The worker owns the live counters. A copy is emitted to the GUI so the GUI
 * never directly accesses socket-thread state.
 */
struct PortStats
{
    quint16 port = 0;
    QString state;
    QString lastError;
    quint64 activeConnections = 0;
    quint64 acceptedConnections = 0;
    quint64 receivedRequests = 0;
    quint64 sentResponses = 0;
    quint64 receivedBytes = 0;
    quint64 sentBytes = 0;
    quint64 protocolErrors = 0;
    double receiveBytesPerSecond = 0.0;
    double sendBytesPerSecond = 0.0;
};

Q_DECLARE_METATYPE(PortStats)

/**
 * @brief Runs one QTcpServer instance in a dedicated QThread.
 *
 * Each worker listens on exactly one address/port pair. It accepts multiple
 * clients, gives each client its own EchoStreamProcessor, and periodically
 * publishes a copy of PortStats to the main window.
 */
class PortServerWorker final : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Creates a worker for one listening endpoint.
     * @param address Local interface address to bind.
     * @param port TCP port to listen on.
     * @param parent Optional Qt object parent.
     */
    explicit PortServerWorker(QHostAddress address, quint16 port, QObject *parent = nullptr);

public Q_SLOTS:
    /**
     * @brief Creates the TCP server and starts its statistics timer.
     */
    void start();

    /**
     * @brief Stops accepting clients and closes all active client sockets.
     */
    void stop();

Q_SIGNALS:
    void statsUpdated(const PortStats &stats);
    void stopped(quint16 port);

private:
    // These methods execute in the worker thread and therefore may access the
    // worker's QTcpServer, QTcpSocket map, and counters directly.
    void acceptConnections();
    void handleReadyRead(QTcpSocket *socket);
    void removeConnection(QTcpSocket *socket);
    void publishStats();

    QHostAddress m_address;
    quint16 m_port = 0;
    QTcpServer *m_server = nullptr;
    QTimer *m_statsTimer = nullptr;
    QHash<QTcpSocket *, EchoStreamProcessor> m_processors;
    PortStats m_stats;
    QElapsedTimer m_rateTimer;
    quint64 m_previousReceivedBytes = 0;
    quint64 m_previousSentBytes = 0;
    bool m_started = false;
    bool m_stopping = false;
};

#endif // PORTSERVERWORKER_H
