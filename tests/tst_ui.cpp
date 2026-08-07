#include <QtTest>

#include <QList>
#include <QString>

#include "logbroker.h"
#include "mainwindow.h"
#include "pagercontroller.h"

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
};

QTEST_GUILESS_MAIN(TestUi)
#include "tst_ui.moc"
