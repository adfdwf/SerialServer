#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "portserverworker.h"

#include <QHash>
#include <QMainWindow>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QThread;

/**
 * @brief Main window for configuring and monitoring multiple TCP echo ports.
 *
 * The window owns one QThread/PortServerWorker pair per configured port. Socket
 * work stays outside the GUI thread, while queued stats signals update the table.
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    /**
     * @brief Builds the configuration controls and statistics table.
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief Stops all workers before the window is destroyed.
     */
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    /** @brief Validates input and starts one worker for every selected port. */
    void startServers();

    /** @brief Requests an asynchronous stop for all configured workers. */
    void stopServers();

    /** @brief Applies a worker's latest statistics snapshot to the table. */
    void updatePortStats(const PortStats &stats);

private:
    struct Endpoint {
        QThread *thread = nullptr;
        PortServerWorker *worker = nullptr;
        int row = -1;
    };

    void createUi();
    void populateAddresses();
    void stopServers(bool waitForCompletion);
    void setRunningUi(bool running);
    void updateSummary();
    void appendLog(const QString &message);
    static QString formatCount(quint64 value);
    static QString formatMiB(quint64 bytes);
    static QString formatRate(double bytesPerSecond);

    QComboBox *m_addressCombo = nullptr;
    QLineEdit *m_portsEdit = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QHash<quint16, Endpoint> m_endpoints;
    QHash<quint16, PortStats> m_latestStats;
    QHash<quint16, QString> m_lastStates;
    bool m_closing = false;
};

#endif // MAINWINDOW_H
