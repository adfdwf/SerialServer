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

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void startServers();
    void stopServers();
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
