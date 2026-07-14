#include "mainwindow.h"

#include "portrangeparser.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

namespace {
// Column indexes are kept in one place so table creation and updates use the
// same layout. Adding a column requires updating both the enum and the header
// labels in createUi().
enum Column {
    PortColumn,
    StateColumn,
    ActiveColumn,
    AcceptedColumn,
    ReceivedRequestsColumn,
    SentResponsesColumn,
    ReceivedMiBColumn,
    SentMiBColumn,
    ReceiveRateColumn,
    SendRateColumn,
    ErrorsColumn,
    ColumnCount
};

/**
 * @brief Creates a centered, non-editable table cell.
 * @param text Initial cell text.
 * @return Newly allocated item owned by the table after insertion.
 */
QTableWidgetItem *makeItem(const QString &text = {})
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}
}

/**
 * @brief Builds the window, controls, table, log, and signal connections.
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    qRegisterMetaType<PortStats>("PortStats");
    createUi();
    populateAddresses();
}

/**
 * @brief Stops all worker threads before Qt destroys the window.
 */
MainWindow::~MainWindow()
{
    m_closing = true;
    stopServers(true);
}

/**
 * @brief Handles a user/window-manager close request.
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    stopServers(true);
    event->accept();
}

/**
 * @brief Creates the configuration form and the live statistics view.
 */
void MainWindow::createUi()
{
    setWindowTitle(QStringLiteral("SerialServer - Multi-Port TCP Benchmark Server"));
    resize(1320, 760);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);

    auto *formLayout = new QFormLayout();
    // The combo is editable because a machine may have an interface that is
    // not enumerated yet or the user may intentionally enter 0.0.0.0.
    m_addressCombo = new QComboBox(central);
    m_addressCombo->setEditable(true);
    m_portsEdit = new QLineEdit(QStringLiteral("1-65534"), central);
    m_portsEdit->setPlaceholderText(QStringLiteral("Example: 1-65534,10200"));
    formLayout->addRow(QStringLiteral("Listen IP:"), m_addressCombo);
    formLayout->addRow(QStringLiteral("Ports:"), m_portsEdit);
    rootLayout->addLayout(formLayout);

    auto *buttonLayout = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("Start Servers"), central);
    m_stopButton = new QPushButton(QStringLiteral("Stop Servers"), central);
    m_stopButton->setEnabled(false);
    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addStretch();
    rootLayout->addLayout(buttonLayout);

    m_summaryLabel = new QLabel(QStringLiteral("Stopped"), central);
    m_summaryLabel->setStyleSheet(QStringLiteral("font-weight: 600; padding: 6px; background: #eef2f6;"));
    rootLayout->addWidget(m_summaryLabel);

    auto *splitter = new QSplitter(Qt::Vertical, central);
    // The table is read-only: all values are snapshots published by workers.
    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Port"),
                                        QStringLiteral("State"),
                                        QStringLiteral("Active"),
                                        QStringLiteral("Accepted"),
                                        QStringLiteral("RX Requests"),
                                        QStringLiteral("TX Responses"),
                                        QStringLiteral("RX MiB"),
                                        QStringLiteral("TX MiB"),
                                        QStringLiteral("RX MiB/s"),
                                        QStringLiteral("TX MiB/s"),
                                        QStringLiteral("Errors")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_log = new QPlainTextEdit(splitter);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    splitter->addWidget(m_table);
    splitter->addWidget(m_log);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(central);
    QObject::connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startServers);
    QObject::connect(m_stopButton, &QPushButton::clicked, this, QOverload<>::of(&MainWindow::stopServers));
}

/**
 * @brief Populates the listen-address selector from local IPv4 interfaces.
 */
void MainWindow::populateAddresses()
{
    // 0.0.0.0 listens on every local IPv4 interface and is convenient when the
    // service must accept clients from another machine.
    m_addressCombo->addItem(QStringLiteral("0.0.0.0"));
    m_addressCombo->addItem(QStringLiteral("127.0.0.1"));

    QSet<QString> known{QStringLiteral("0.0.0.0"), QStringLiteral("127.0.0.1")};
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol) {
            continue;
        }
        const QString text = address.toString();
        if (!known.contains(text)) {
            known.insert(text);
            m_addressCombo->addItem(text);
        }
    }
    m_addressCombo->setCurrentText(QStringLiteral("0.0.0.0"));
}

/**
 * @brief Validates the form and starts one worker per requested port.
 */
void MainWindow::startServers()
{
    if (!m_endpoints.isEmpty()) {
        return;
    }

    QHostAddress address;
    if (!address.setAddress(m_addressCombo->currentText().trimmed())) {
        QMessageBox::warning(this, QStringLiteral("Invalid IP"), QStringLiteral("Enter a valid IPv4 listen address."));
        return;
    }

    const PortParseResult parsed = PortRangeParser::parse(m_portsEdit->text());
    if (!parsed.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Invalid ports"), parsed.error);
        return;
    }

    m_table->setRowCount(0);
    m_latestStats.clear();
    m_lastStates.clear();
    setRunningUi(true);
    appendLog(QStringLiteral("Starting %1 ports on %2").arg(parsed.ports.size()).arg(address.toString()));

    // Each port gets an independent thread and QTcpServer. A failure on one
    // port is therefore visible in that row without stopping other ports.
    for (quint16 port : parsed.ports) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column) {
            m_table->setItem(row, column, makeItem());
        }
        m_table->item(row, PortColumn)->setText(QString::number(port));
        m_table->item(row, StateColumn)->setText(QStringLiteral("Starting"));

        auto *thread = new QThread(this);
        auto *worker = new PortServerWorker(address, port);
        worker->moveToThread(thread);

        Endpoint endpoint;
        endpoint.thread = thread;
        endpoint.worker = worker;
        endpoint.row = row;
        m_endpoints.insert(port, endpoint);

        QObject::connect(thread, &QThread::started, worker, &PortServerWorker::start);
        QObject::connect(worker, &PortServerWorker::statsUpdated, this, &MainWindow::updatePortStats);
        QObject::connect(worker, &PortServerWorker::stopped, thread, &QThread::quit, Qt::DirectConnection);
        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(thread, &QThread::finished, this, [this, thread, port]() {
            m_endpoints.remove(port);
            thread->deleteLater();
            if (m_endpoints.isEmpty() && !m_closing) {
                setRunningUi(false);
                updateSummary();
                appendLog(QStringLiteral("All servers stopped"));
            }
        });
        thread->start();
    }
}

/**
 * @brief Public slot used by the Stop button.
 */
void MainWindow::stopServers()
{
    stopServers(false);
}

/**
 * @brief Requests all workers to stop, optionally waiting during destruction.
 * @param waitForCompletion True when the window is being destroyed.
 */
void MainWindow::stopServers(bool waitForCompletion)
{
    if (m_endpoints.isEmpty()) {
        if (!m_closing) {
            setRunningUi(false);
        }
        return;
    }

    m_stopButton->setEnabled(false);
    if (!m_closing) {
        appendLog(QStringLiteral("Stopping servers..."));
    }

    // Take a copy because worker-finished callbacks remove entries from the
    // live hash while shutdown proceeds.
    const QList<Endpoint> endpoints = m_endpoints.values();
    for (const Endpoint &endpoint : endpoints) {
        if (!endpoint.thread || !endpoint.worker || !endpoint.thread->isRunning()) {
            continue;
        }
        QMetaObject::invokeMethod(endpoint.worker,
                                  "stop",
                                  waitForCompletion ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
    }

    if (waitForCompletion) {
        for (const Endpoint &endpoint : endpoints) {
            if (!endpoint.thread || !endpoint.thread->isRunning()) {
                continue;
            }
            endpoint.thread->quit();
            endpoint.thread->wait(5000);
        }
        m_endpoints.clear();
    }
}

/**
 * @brief Updates one table row and refreshes the aggregate summary.
 * @param stats Latest snapshot emitted by a PortServerWorker.
 */
void MainWindow::updatePortStats(const PortStats &stats)
{
    const auto endpoint = m_endpoints.constFind(stats.port);
    if (endpoint == m_endpoints.cend() || endpoint->row < 0) {
        return;
    }

    const int row = endpoint->row;
    m_latestStats.insert(stats.port, stats);
    m_table->item(row, StateColumn)->setText(stats.state);
    m_table->item(row, ActiveColumn)->setText(formatCount(stats.activeConnections));
    m_table->item(row, AcceptedColumn)->setText(formatCount(stats.acceptedConnections));
    m_table->item(row, ReceivedRequestsColumn)->setText(formatCount(stats.receivedRequests));
    m_table->item(row, SentResponsesColumn)->setText(formatCount(stats.sentResponses));
    m_table->item(row, ReceivedMiBColumn)->setText(formatMiB(stats.receivedBytes));
    m_table->item(row, SentMiBColumn)->setText(formatMiB(stats.sentBytes));
    m_table->item(row, ReceiveRateColumn)->setText(formatRate(stats.receiveBytesPerSecond));
    m_table->item(row, SendRateColumn)->setText(formatRate(stats.sendBytesPerSecond));
    m_table->item(row, ErrorsColumn)->setText(formatCount(stats.protocolErrors));
    m_table->item(row, StateColumn)->setToolTip(stats.lastError);

    if (m_lastStates.value(stats.port) != stats.state) {
        QString message = QStringLiteral("Port %1: %2").arg(stats.port).arg(stats.state);
        if (!stats.lastError.isEmpty()) {
            message += QStringLiteral(" - ") + stats.lastError;
        }
        appendLog(message);
        m_lastStates.insert(stats.port, stats.state);
    }
    updateSummary();
}

/**
 * @brief Enables/disables controls according to whether workers are running.
 */
void MainWindow::setRunningUi(bool running)
{
    m_addressCombo->setEnabled(!running);
    m_portsEdit->setEnabled(!running);
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
}

/**
 * @brief Recalculates totals across all configured ports.
 */
void MainWindow::updateSummary()
{
    quint64 active = 0;
    quint64 requests = 0;
    quint64 responses = 0;
    quint64 receivedBytes = 0;
    quint64 sentBytes = 0;
    quint64 errors = 0;
    double receiveRate = 0.0;
    double sendRate = 0.0;
    int listening = 0;

    for (const PortStats &stats : m_latestStats) {
        active += stats.activeConnections;
        requests += stats.receivedRequests;
        responses += stats.sentResponses;
        receivedBytes += stats.receivedBytes;
        sentBytes += stats.sentBytes;
        errors += stats.protocolErrors;
        receiveRate += stats.receiveBytesPerSecond;
        sendRate += stats.sendBytesPerSecond;
        if (stats.state == QStringLiteral("Listening")) {
            ++listening;
        }
    }

    m_summaryLabel->setText(
        QStringLiteral("Listening %1/%2 | Active %3 | RX requests %4 | TX responses %5 | "
                       "RX %6 MiB (%7 MiB/s) | TX %8 MiB (%9 MiB/s) | Errors %10")
            .arg(listening)
            .arg(m_latestStats.size())
            .arg(active)
            .arg(requests)
            .arg(responses)
            .arg(formatMiB(receivedBytes))
            .arg(formatRate(receiveRate))
            .arg(formatMiB(sentBytes))
            .arg(formatRate(sendRate))
            .arg(errors));
}

/**
 * @brief Appends a timestamped message to the bounded log widget.
 */
void MainWindow::appendLog(const QString &message)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2")
                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), message));
}

/**
 * @brief Formats a counter without changing its unit.
 */
QString MainWindow::formatCount(quint64 value)
{
    return QString::number(value);
}

/**
 * @brief Converts bytes to mebibytes with three fractional digits.
 */
QString MainWindow::formatMiB(quint64 bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 3);
}

/**
 * @brief Converts bytes per second to mebibytes per second for display.
 */
QString MainWindow::formatRate(double bytesPerSecond)
{
    return QString::number(bytesPerSecond / (1024.0 * 1024.0), 'f', 3);
}
