#include <QtTest>

#include <QList>
#include <QString>

#include "config.h"
#include "logbroker.h"
#include "mainwindow.h"
#include "pagercontroller.h"
#include "traymenu.h"

// Unit tests for the UI's pure text-formatting logic (Task 001-6). The status
// window and tray are driven by direct signal/slot connections and are verified
// live (see the task's Verification section); here we lock down the string
// formatting ported from overview.py so the singular/plural, Last Number line,
// and newline-joined queue never regress. These statics touch no widgets, so
// the suite runs guiless.
class TestUi : public QObject
{
    Q_OBJECT

private slots:
    // --- formatActiveLine (ports overview.py active/last block) ------------

    // Nothing showing and no history: the N/A placeholder.
    void active_idleShowsNA()
    {
        QCOMPARE(MainWindow::formatActiveLine(QString(), QString()),
                 QString("Active Number: N/A"));
    }

    // A single number is singular.
    void active_singleIsSingular()
    {
        QCOMPARE(MainWindow::formatActiveLine(QString(), QString("1234")),
                 QString("Active Number: 1234"));
    }

    // A batch of several pluralizes to "Active Numbers:" (was len(nonce) > 1).
    void active_multipleIsPlural()
    {
        QCOMPARE(MainWindow::formatActiveLine(QString(), QString("10 & 20")),
                 QString("Active Numbers: 10 & 20"));
    }

    // A prior number prepends a "Last Number:" line above the active line.
    void active_lastNumberPrependedWhenShowing()
    {
        QCOMPARE(MainWindow::formatActiveLine(QString("999"), QString("1234")),
                 QString("Last Number: 999\nActive Number: 1234"));
    }

    // The Last Number line shows even while nothing is on screen.
    void active_lastNumberWithNothingActive()
    {
        QCOMPARE(MainWindow::formatActiveLine(QString("555"), QString()),
                 QString("Last Number: 555\nActive Number: N/A"));
    }

    // --- formatQueue (ports overview.py queued block) ---------------------

    // An empty queue still shows the header.
    void queue_emptyShowsHeaderOnly()
    {
        QCOMPARE(MainWindow::formatQueue({}), QString("Numbers Queued:"));
    }

    // Batches render newline-joined, each via PagerController::formatBatch.
    void queue_batchesNewlineJoined()
    {
        const QList<PagerController::Batch> batches{
            PagerController::Batch{{"m1", "1"}, {"m2", "2"}},
            PagerController::Batch{{"m3", "3"}}};
        QCOMPARE(MainWindow::formatQueue(batches),
                 QString("Numbers Queued:\n1 & 2\n3"));
    }

    // --- LogBroker::formatLine (feeds the in-window Log tab) ---------------

    // Warnings/criticals map to the WARN/ERROR levels the Log tab shows so a
    // ProPresenter/Slack failure reads as actionable text, not a dead modal.
    void logLine_levelPrefixes()
    {
        QCOMPARE(LogBroker::formatLine(QtWarningMsg,
                                       QStringLiteral("[slack] disconnected")),
                 QString("[WARN] [slack] disconnected"));
        QCOMPARE(LogBroker::formatLine(QtCriticalMsg, QStringLiteral("boom")),
                 QString("[ERROR] boom"));
        QCOMPARE(LogBroker::formatLine(QtInfoMsg, QStringLiteral("hi")),
                 QString("[INFO] hi"));
    }

    // --- TrayMenu::warningTooltip (Task 002-6 warning-state precedence) -----
    //
    // The tray's warning indicator is driven by three inputs — config validity,
    // Slack connected, ProPresenter connected. warningTooltip() is the pure
    // decision: it returns the reason string when the tray should warn, or an
    // empty QString when it should show the normal state. These statics touch no
    // widgets, matching the guiless verification of the format helpers above.

    // All good → normal state (no warning).
    void tray_allGoodIsNormal()
    {
        QCOMPARE(TrayMenu::warningTooltip(true, QString(), true, true),
                 QString());
    }

    // A present-but-malformed-only result still reports configValid==true (no
    // required key unset), so with both clients up the tray stays normal.
    void tray_shapeWarningOnlyIsNormal()
    {
        QCOMPARE(TrayMenu::warningTooltip(true, QString(), true, true),
                 QString());
    }

    // Invalid config surfaces the validation summary verbatim as the tooltip.
    void tray_invalidConfigShowsSummary()
    {
        QCOMPARE(TrayMenu::warningTooltip(
                     false, QStringLiteral("Slack bot token is required"), true,
                     true),
                 QString("Slack bot token is required"));
    }

    // Missing config outranks a client being down (most actionable first).
    void tray_configPrecedesClientDown()
    {
        QCOMPARE(TrayMenu::warningTooltip(
                     false, QStringLiteral("Slack bot token is required"), false,
                     false),
                 QString("Slack bot token is required"));
    }

    // Invalid config with no summary falls back to a generic message.
    void tray_invalidConfigNoSummaryFallback()
    {
        QCOMPARE(TrayMenu::warningTooltip(false, QString(), true, true),
                 QString("Configuration incomplete"));
    }

    // Config valid but Slack down.
    void tray_slackDown()
    {
        QCOMPARE(TrayMenu::warningTooltip(true, QString(), false, true),
                 QString("Slack disconnected"));
    }

    // Config valid but ProPresenter down.
    void tray_proPresDown()
    {
        QCOMPARE(TrayMenu::warningTooltip(true, QString(), true, false),
                 QString("ProPresenter disconnected"));
    }

    // Config valid but both clients down.
    void tray_bothDown()
    {
        QCOMPARE(TrayMenu::warningTooltip(true, QString(), false, false),
                 QString("Slack and ProPresenter disconnected"));
    }

    // --- MainWindow::bannerContent (Task 002-5 validation banner) ----------
    //
    // The window's validation banner (Spec 002 Decision 8) is driven by a pure
    // decision that maps a Config::ValidationResult to a tier + message, so the
    // "dismissible-on-fix" and error-vs-warning tiering can be locked down
    // guiless — same pattern as warningTooltip above. showValidation() only
    // applies visibility/styling from this result.

    // A fully-valid config yields no banner (dismissible-on-fix).
    void banner_cleanIsHidden()
    {
        Config::ValidationResult clean;
        const MainWindow::BannerContent c = MainWindow::bannerContent(clean);
        QCOMPARE(c.tier, MainWindow::BannerTier::None);
        QCOMPARE(c.message, QString());
    }

    // Required-but-unset keys produce an error-styled banner naming each one.
    void banner_requiredMissingIsError()
    {
        Config::ValidationResult r;
        r.requiredMissing.append({QStringLiteral("slack/bot_token"),
                                  QStringLiteral("Slack bot token is required")});
        r.requiredMissing.append(
            {QStringLiteral("slack/listen_channel"),
             QStringLiteral("Slack listen channel is required")});
        const MainWindow::BannerContent c = MainWindow::bannerContent(r);
        QCOMPARE(c.tier, MainWindow::BannerTier::Error);
        QCOMPARE(c.message,
                 QString("Fix these to connect:\nSlack bot token is required\n"
                         "Slack listen channel is required"));
    }

    // Only shape-warnings produce a softer warning-styled banner.
    void banner_shapeWarningIsWarning()
    {
        Config::ValidationResult r;
        r.shapeWarnings.append(
            {QStringLiteral("slack/bot_token"),
             QStringLiteral("Bot token usually starts with \"xoxb-\"")});
        const MainWindow::BannerContent c = MainWindow::bannerContent(r);
        QCOMPARE(c.tier, MainWindow::BannerTier::Warning);
        QCOMPARE(c.message,
                 QString("These settings look off (still connecting):\n"
                         "Bot token usually starts with \"xoxb-\""));
    }

    // Required-missing takes visual precedence over shape-warnings: when both
    // exist the banner is error-styled and lists only the blockers.
    void banner_requiredMissingPrecedesWarnings()
    {
        Config::ValidationResult r;
        r.requiredMissing.append({QStringLiteral("slack/bot_token"),
                                  QStringLiteral("Slack bot token is required")});
        r.shapeWarnings.append(
            {QStringLiteral("slack/listen_channel"),
             QStringLiteral("Channel ID usually starts with \"C\"")});
        const MainWindow::BannerContent c = MainWindow::bannerContent(r);
        QCOMPARE(c.tier, MainWindow::BannerTier::Error);
        QCOMPARE(c.message,
                 QString("Fix these to connect:\nSlack bot token is required"));
    }
};

QTEST_GUILESS_MAIN(TestUi)
#include "tst_ui.moc"
