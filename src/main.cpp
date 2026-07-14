#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

/**
 * @brief Application entry point for the GUI benchmark server.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument array supplied by the operating system.
 * @return Qt event-loop exit code.
 *
 * All server workers are owned by the main window and run in their own
 * threads. The event loop must remain active so Qt can deliver socket,
 * timer, and cross-thread signals.
 */
int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SerialServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    MainWindow window;
    window.show();
    return application.exec();
}
