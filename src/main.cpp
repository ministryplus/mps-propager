#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMutex>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextStream>

#include "config.h"
#include "pagercontroller.h"
#include "propresenterclient.h"

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
    pager.start();

    // Menu-bar / tray app: closing or hiding a window must not quit the process.
    app.setQuitOnLastWindowClosed(false);

    // Placeholder tray icon (real assets are Task 8). A concrete icon is needed
    // or the tray item may not render on some platforms.
    const QIcon icon = app.style()->standardIcon(QStyle::SP_ComputerIcon);
    QSystemTrayIcon tray(icon);
    tray.setToolTip("ProPager");

    QMenu menu;
    QAction *quitAction = menu.addAction("Quit");
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    tray.setContextMenu(&menu);

    tray.setVisible(true);

    // Start minimized to tray: no main window is shown in this task.
    return app.exec();
}
