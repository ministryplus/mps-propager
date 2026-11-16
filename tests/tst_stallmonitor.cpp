#include <QtTest>

#include <QThread>

#include "stallmonitor.h"

// StallMonitor reports when the app's event loop ran late (App Nap throttling,
// or anything else that stops timers firing on time), so the log can explain a
// gap. A blocked event loop stands in for a napped one here.
class TestStallMonitor : public QObject
{
    Q_OBJECT

private slots:
    // On-time ticks report nothing.
    void quiet_whenTimersRunOnTime()
    {
        StallMonitor monitor(/*intervalMs=*/20, /*thresholdMs=*/150);
        QSignalSpy stalled(&monitor, &StallMonitor::stalled);
        monitor.start();

        QTest::qWait(300);
        QCOMPARE(stalled.size(), 0);
    }

    // A loop held up well past the threshold is reported once, with roughly
    // how late the timer ran.
    void reports_whenEventLoopIsHeldUp()
    {
        StallMonitor monitor(/*intervalMs=*/20, /*thresholdMs=*/150);
        QSignalSpy stalled(&monitor, &StallMonitor::stalled);
        monitor.start();

        QTimer::singleShot(30, [] { QThread::msleep(400); });

        QTRY_COMPARE_WITH_TIMEOUT(stalled.size(), 1, 2000);
        const qint64 lateMs = stalled.first().first().toLongLong();
        QVERIFY2(lateMs >= 300 && lateMs < 1000, qPrintable(QString::number(lateMs)));
        QTest::qWait(100);
        QCOMPARE(stalled.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestStallMonitor)
#include "tst_stallmonitor.moc"
