#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QTextStream>
#include <QTime>

#include "config.h"
#include "logbroker.h"
#include "pagercontroller.h"
#include "propresenterclient.h"
#include "slackclient.h"
#include "ui/connectionstab.h"
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

    const QString line = LogBroker::formatLine(type, msg, QTime::currentTime());

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
    // Surfaced in the tray's "About ProPager" dialog. Single source of truth is
    // the CMake project(... VERSION) — wired in via the PROPAGER_APP_VERSION
    // compile definition (see CMakeLists.txt), so this never drifts from the
    // bundle's CFBundleShortVersionString.
    QCoreApplication::setApplicationVersion(QStringLiteral(PROPAGER_APP_VERSION));

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
    MainWindow window(&pager, &config, config.configDir());
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

    // --- Connections tab: config/validation/reconnect wiring (Task 002-7) ---
    //
    // Spec 002 Decisions 3/6/7: Save, Reconnect, and the startup gate are the
    // SAME validate -> (write) -> (reconnect-if-allowed) path with different
    // inputs. The pieces below implement that path once (as small lambdas) so
    // all three routes share it rather than duplicating the logic three ways.
    ConnectionsTab *tab = window.connectionsTab();

    // Does `section` carry a required-but-unset error in `result`? This is the
    // single gate rule reused everywhere: Save's reconnect gate (step 3c), the
    // Reconnect button, the tray, and the startup Slack gate (step 7) all ask
    // it. requiredMissing keys are namespaced ("slack/…", "propresenter/…"), so
    // a section is blocked only by its own missing keys.
    const auto sectionBlocked = [](const Config::ValidationResult &result,
                                   ConnectionsTab::Section section) {
        const QString prefix = section == ConnectionsTab::Section::Slack
                                   ? QStringLiteral("slack/")
                                   : QStringLiteral("propresenter/");
        for (const Config::ValidationEntry &entry : result.requiredMissing) {
            if (entry.key.startsWith(prefix))
                return true;
        }
        return false;
    };

    // Fan one validation result out to all three cross-cutting surfaces
    // (Decision 8): inline field markers on the tab, the window banner, and the
    // tray warning state. Called at startup and after every Save.
    const auto publishValidation = [&](const Config::ValidationResult &result) {
        tab->showValidation(result);
        window.showValidation(result);
        tray.setConfigValidation(result);
    };

    // Reconnect one section's client from last-saved Config, gated on that
    // section having no required-but-unset error. The Decision-5 footgun lives
    // in the CALLERS: only Save (with a changed connection field) and the
    // explicit Reconnect/tray actions call this — a behavior-only Save never
    // does, so an on-screen number is never wiped by a stray ensureMessage().
    const auto reconnectSection = [&](ConnectionsTab::Section section,
                                      const Config::ValidationResult &result) {
        if (sectionBlocked(result, section))
            return;
        if (section == ConnectionsTab::Section::Slack)
            slack.reconnectNow();
        else
            proPresenter.reconnect();
    };

    // Save one section (step 3). Always writes + sync + reload (typing is never
    // discarded, even when a required field is blank — Decision 6), reloads
    // behavior timings into the controller with no socket churn, and reconnects
    // ONLY when a connection field actually changed AND the section is unblocked.
    const auto saveSection = [&](ConnectionsTab::Section section) {
        // Read the dirty flag BEFORE commitBaseline() clears it below.
        const bool connectionDirty =
            section == ConnectionsTab::Section::Slack
                ? tab->slackConnectionDirty()
                : tab->proPresConnectionDirty();

        // (b) Persist every field in the section, required-or-not.
        if (section == ConnectionsTab::Section::Slack) {
            config.setSlackBotToken(tab->slackBotToken());
            config.setSlackAppToken(tab->slackAppToken());
            config.setSlackListenChannel(tab->slackListenChannel());
            // The tab presents ignore-numbers comma-joined; split back to the
            // QStringList Config stores (trim, drop empties — mirrors the read
            // path in Config::slackIgnoreNumbers()).
            QStringList ignore;
            const QStringList parts = tab->slackIgnoreNumbers().split(
                QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                const QString trimmed = part.trimmed();
                if (!trimmed.isEmpty())
                    ignore << trimmed;
            }
            config.setSlackIgnoreNumbers(ignore);
        } else {
            config.setPropresenterHost(tab->proPresHost());
            config.setPropresenterPort(tab->proPresPort());
            config.setBatchWaitTime(tab->batchWaitTime());
            config.setBatchMaxCount(tab->batchMaxCount());
            config.setExpireTime(tab->expireTime());
        }
        config.reload();

        // (a/e) Re-validate the just-written config and refresh every surface.
        const Config::ValidationResult result = config.validate();
        publishValidation(result);

        // (d) Behavior edits (ProPresenter section) reload into the controller
        // via the Task 002-2 setters — no reconnect, no socket churn. Config
        // stores seconds; the timers take milliseconds.
        if (section == ConnectionsTab::Section::ProPresenter) {
            pager.setBatchWaitMs(config.batchWaitTime() * 1000);
            pager.setBatchMaxCount(config.batchMaxCount());
            pager.setExpireMs(config.expireTime() * 1000);
        }

        // (c) The reconnect gate (Decision 5): connection field changed AND the
        // section is unblocked. An expire-time-only Save has connectionDirty ==
        // false, so it never reconnects and leaves a live number on screen.
        if (connectionDirty)
            reconnectSection(section, result);

        // The section's current text is now the saved baseline.
        tab->commitBaseline(section);
    };

    // Save / Reconnect / Test from the Connections tab.
    QObject::connect(tab, &ConnectionsTab::saveRequested, &app, saveSection);
    // Reconnect button = Save-with-nothing-dirty (Decision 3): no field write,
    // just force-reconnect that one client from saved config, still gated.
    QObject::connect(tab, &ConnectionsTab::reconnectRequested, &app,
                     [&, reconnectSection](ConnectionsTab::Section section) {
                         const Config::ValidationResult result =
                             config.validate();
                         publishValidation(result);
                         reconnectSection(section, result);
                     });
    // Test = reachability only (Decision 10); result surfaces via tested().
    QObject::connect(tab, &ConnectionsTab::testRequested, &proPresenter,
                     [&proPresenter] { proPresenter.test(); });
    // Route the structured Test result to the Log tab / on-disk log (the detail
    // string is documented "for the tab/log"); reachable outcomes as info,
    // failures as warnings so they stand out.
    QObject::connect(&proPresenter, &ProPresenterClient::tested, &app,
                     [](const ProPresenterClient::TestResult &result) {
                         if (result.reachable && result.messageReady)
                             qInfo().noquote() << result.detail;
                         else
                             qWarning().noquote() << result.detail;
                     });

    // Tray Reconnect (Decision 12): coarse "kick both clients" from saved
    // config, each still gated on its section not being required-but-unset.
    QObject::connect(&tray, &TrayMenu::reconnectRequested, &app, [&] {
        const Config::ValidationResult result = config.validate();
        publishValidation(result);
        reconnectSection(ConnectionsTab::Section::Slack, result);
        reconnectSection(ConnectionsTab::Section::ProPresenter, result);
    });

    // Startup clear (Decision 5) always runs, so launch begins known-empty and
    // ProPresenter ensures its message (host/port are optional-with-default).
    pager.start();

    // Startup validation gate (Decisions 7/8) — the SAME rule as the Save
    // reconnect gate. Fan the load-time result to banner + tray so a first-run
    // empty config warns without opening the tab, and only start the Slack
    // backoff loop when the required Slack keys are present. This kills the
    // first-run "apps.connections.open returned no URL:" spam (#5).
    const Config::ValidationResult startupValidation = config.validate();
    publishValidation(startupValidation);
    if (!sectionBlocked(startupValidation, ConnectionsTab::Section::Slack))
        slack.start();

    return app.exec();
}
