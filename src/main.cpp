#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>

#include "config.h"
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

    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }

    const QString line = QStringLiteral("[%1] %2").arg(level, msg);

    // Always echo to stderr so a dev launch still shows output.
    QTextStream(stderr) << line << '\n';

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream(g_logFile) << line << '\n';
        g_logFile->flush();
    }
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
    MainWindow window(&pager);
    TrayMenu tray(&pager);

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

    // Fatal config/connection errors surface as a modal (mainwindow.py parity).
    // NOTE (Task 6 open item): both clients also emit error() for transient
    // failures (reconnect backoff, a failed reactions.add). Routing every one
    // to the modal can spam during a network blip; distinguishing transient
    // from fatal needs a separate client-side signal and is left as a follow-up.
    QObject::connect(&slack, &SlackClient::error, &window,
                     &MainWindow::showSetupError);
    QObject::connect(&proPresenter, &ProPresenterClient::error, &window,
                     &MainWindow::showSetupError);

    // Startup clear (Decision 5), then connect to Slack. The app starts in the
    // tray; the window is shown on demand via Open Window.
    pager.start();
    slack.start();

    return app.exec();
}
