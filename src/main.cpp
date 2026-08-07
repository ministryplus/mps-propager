#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QTextStream>

#include "config.h"
#include "logbroker.h"
#include "pagercontroller.h"
#include "propresenterclient.h"
#include "slackclient.h"
#include "ui/mainwindow.h"
#include "ui/traymenu.h"

namespace {

// The single log file, opened once against the app-support/ProPager directory
// (Decision 11 — never ~/Documents). Guarded by a mutex since Qt may deliver
// messages from more than one place.
QFile *g_logFile = nullptr;
QMutex g_logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    QMutexLocker locker(&g_logMutex);

    const QString line = LogBroker::formatLine(type, msg);

    // Always echo to stderr so a dev launch still shows output.
    QTextStream(stderr) << line << '\n';

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream(g_logFile) << line << '\n';
        g_logFile->flush();
    }

    // Fan the same line out to the in-window Log tab. Receivers use a queued
    // connection, so this is safe even if a message ever arrives off the GUI
    // thread.
    LogBroker::instance()->post(line);
}

// Open the log file under the config directory, creating the directory if
// needed, and route qDebug/qInfo/etc. through the handler above.
void installFileLogging(const QString &configDir)
{
    QDir().mkpath(configDir);
    g_logFile = new QFile(configDir + QStringLiteral("/ProPager.log"));
    g_logFile->open(QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set the QSettings identity before anything reads settings (Decision 11),
    // so the on-disk location is correct from the very first launch.
    QCoreApplication::setOrganizationName("com.isaacwiebe");
    QCoreApplication::setApplicationName("ProPager");

    // Show DEBUG+ in the Log tab and on-disk log. Uncategorized qDebug() is
    // normally already delivered, but make it explicit so a release build or a
    // stray QT_LOGGING_RULES can never silently drop debug lines. Scoped to the
    // "default" (uncategorized) category so Qt-internal debug (qt.network.*, …)
    // stays quiet — we only want the app's own qDebug() output.
    QLoggingCategory::setFilterRules(QStringLiteral("default.debug=true"));

    // Create the log broker in the GUI thread up front so it has the right
    // thread affinity before the message handler (which may run early) posts.
    LogBroker::instance();

    // Load configuration and route logging into the same app-support/ProPager
    // directory as the .ini (Decision 10/11, closes Open TODO 2).
    Config config;
    config.load();
    installFileLogging(config.configDir());
    qInfo() << "ProPager config:" << config.configPath();
    qInfo() << "ProPager log dir:" << config.configDir();

    // Wire the coordinating core (Task 001-4). The ProPresenter client owns the
    // REST message lifecycle; the controller batches numbers and drives it on
    // the single Qt event loop (no threads). Config stores the timings in
    // seconds — convert to the milliseconds the QTimers expect. start() issues
    // the startup clear (Decision 5) so launch begins from a known-empty state.
    // The Slack layer that feeds enqueueNumber() arrives in a later wave.
    ProPresenterClient proPresenter(config);
    PagerController pager(&proPresenter,
                          config.batchWaitTime() * 1000,
                          config.batchMaxCount(),
                          config.expireTime() * 1000);

    // Slack layer (Tasks 001-5/001-7). Passing the controller lets SlackClient
    // enqueue pages and route emoji feedback internally — none of that wiring
    // lives here (Task 7 owns it), keeping main.cpp purely about UI + lifecycle.
    SlackClient slack(config, &pager);

    // Menu-bar / tray app: closing or hiding a window must not quit the process.
    app.setQuitOnLastWindowClosed(false);

    // UI (Task 001-6). Both surfaces update by direct signal/slot connections
    // (Decision 7) — there is no polling thread.
    MainWindow window(&pager, config.configDir());
    TrayMenu tray(&pager);

    // Stream the app's error/access log into the window's Log tab. Queued so
    // delivery always lands on the GUI thread.
    QObject::connect(LogBroker::instance(), &LogBroker::appended, &window,
                     &MainWindow::appendLog, Qt::QueuedConnection);

    // Connection state -> status labels/actions on both surfaces.
    QObject::connect(&slack, &SlackClient::connected, &window,
                     [&window] { window.setSlackConnected(true); });
    QObject::connect(&slack, &SlackClient::disconnected, &window,
                     [&window] { window.setSlackConnected(false); });
    QObject::connect(&slack, &SlackClient::connected, &tray,
                     [&tray] { tray.setSlackConnected(true); });
    QObject::connect(&slack, &SlackClient::disconnected, &tray,
                     [&tray] { tray.setSlackConnected(false); });

    QObject::connect(&proPresenter, &ProPresenterClient::connected, &window,
                     [&window] { window.setProPresConnected(true); });
    QObject::connect(&proPresenter, &ProPresenterClient::disconnected, &window,
                     [&window] { window.setProPresConnected(false); });
    QObject::connect(&proPresenter, &ProPresenterClient::connected, &tray,
                     [&tray] { tray.setProPresConnected(true); });
    QObject::connect(&proPresenter, &ProPresenterClient::disconnected, &tray,
                     [&tray] { tray.setProPresConnected(false); });

    // Controller state transitions -> re-render active/queue on both surfaces.
    for (const auto signal : {&PagerController::queued, &PagerController::onScreen,
                              &PagerController::cleared}) {
        QObject::connect(&pager, signal, &window, &MainWindow::refresh);
        QObject::connect(&pager, signal, &tray, &TrayMenu::refreshActive);
    }

    // Open Window from the tray shows/raises the status window.
    QObject::connect(&tray, &TrayMenu::openWindowRequested, &window, [&window] {
        window.show();
        window.raise();
        window.activateWindow();
    });

    // Client errors go to the log, not a modal. A QMessageBox spins a nested
    // event loop, so a burst of error() signals (reconnect backoff, failed
    // reactions.add, timed-out requests) stacked modals and could wedge the UI.
    // Routing them through qWarning() surfaces them in the Log tab (and the
    // on-disk log) as actionable text, non-blocking.
    const auto logClientError = [](const QString &message) {
        qWarning().noquote() << message;
    };
    QObject::connect(&slack, &SlackClient::error, &app, logClientError);
    QObject::connect(&proPresenter, &ProPresenterClient::error, &app,
                     logClientError);

    // Startup clear (Decision 5), then connect to Slack. The app starts in the
    // tray; the window is shown on demand via Open Window.
    pager.start();
    slack.start();

    return app.exec();
}
