#include <QtTest>

#include "connectionwatchdog.h"

// ConnectionWatchdog decides when a Socket Mode connection is dead without the
// OS saying so (the production failure: the app sat "connected" for hours after
// a half-open socket). These run the real timers at millisecond scale.
class TestConnectionWatchdog : public QObject
{
    Q_OBJECT

private:
    static ConnectionWatchdog::Timing fast()
    {
        ConnectionWatchdog::Timing t;
        t.pingIntervalMs = 20;
        t.staleAfterMs = 80;
        t.helloDeadlineMs = 60;
        return t;
    }

private slots:
    // A socket Slack never confirms (no `hello`) is declared dead.
    void expires_whenHelloNeverArrives()
    {
        ConnectionWatchdog dog(fast());
        QSignalSpy expired(&dog, &ConnectionWatchdog::expired);
        dog.start();
        // Activity alone (e.g. pongs) does not substitute for Slack's hello.
        QTimer feeder;
        connect(&feeder, &QTimer::timeout, &dog, &ConnectionWatchdog::noteActivity);
        feeder.start(10);

        QTRY_COMPARE_WITH_TIMEOUT(expired.size(), 1, 1000);
        QVERIFY(expired.first().first().toString().contains("hello"));
        QVERIFY(!dog.isRunning());
    }

    // After hello, silence beyond staleAfterMs is the half-open case.
    void expires_whenSilentAfterHello()
    {
        ConnectionWatchdog dog(fast());
        QSignalSpy expired(&dog, &ConnectionWatchdog::expired);
        dog.start();
        dog.noteHello();

        QTRY_COMPARE_WITH_TIMEOUT(expired.size(), 1, 1000);
        QVERIFY(expired.first().first().toString().contains("no traffic"));
    }

    // A healthy connection (steady pongs/frames) never expires, and the owner
    // is prompted to ping on the interval.
    void staysAlive_withSteadyActivity()
    {
        ConnectionWatchdog dog(fast());
        QSignalSpy expired(&dog, &ConnectionWatchdog::expired);
        QSignalSpy pings(&dog, &ConnectionWatchdog::pingDue);
        dog.start();
        dog.noteHello();
        QTimer feeder;
        connect(&feeder, &QTimer::timeout, &dog, &ConnectionWatchdog::noteActivity);
        feeder.start(15);

        QTest::qWait(400); // ~5x the stale window
        QCOMPARE(expired.size(), 0);
        QVERIFY(pings.size() >= 3);
        QVERIFY(dog.isRunning());
    }

    // A stopped watchdog (retired socket) neither pings nor expires.
    void stop_silencesEverything()
    {
        ConnectionWatchdog dog(fast());
        QSignalSpy expired(&dog, &ConnectionWatchdog::expired);
        QSignalSpy pings(&dog, &ConnectionWatchdog::pingDue);
        dog.start();
        dog.stop();

        QTest::qWait(250);
        QCOMPARE(expired.size(), 0);
        QCOMPARE(pings.size(), 0);
        QVERIFY(!dog.isRunning());
    }

    // Expiry is reported exactly once, so the owner reconnects exactly once.
    void expires_onlyOnce()
    {
        ConnectionWatchdog dog(fast());
        QSignalSpy expired(&dog, &ConnectionWatchdog::expired);
        dog.start();

        QTRY_COMPARE_WITH_TIMEOUT(expired.size(), 1, 1000);
        QTest::qWait(250);
        QCOMPARE(expired.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestConnectionWatchdog)
#include "tst_connectionwatchdog.moc"
