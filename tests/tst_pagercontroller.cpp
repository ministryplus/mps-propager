#include <QtTest>

#include <QSignalSpy>
#include <QString>
#include <QStringList>

#include "config.h"
#include "pagercontroller.h"
#include "propresenterclient.h"

// Unit tests for PagerController (Task 001-4). The controller runs entirely on
// the Qt event loop — a batch-window QTimer and an expiry QTimer — so these
// tests drive it with fake numbers and a recording ProPresenterClient, then
// QTest::qWait() lets the single event loop fire the timers. No Slack and no
// live ProPresenter are involved (see the task's Verification section).

namespace {

// A ProPresenterClient stand-in that performs no network I/O and instead
// records the order of lifecycle calls (Decision 5: PUT -> trigger -> clear)
// plus the numbers it was asked to display. Overriding the virtual action
// slots is enough; the base ctor just needs a throwaway Config.
class RecordingProPresenter : public ProPresenterClient
{
public:
    explicit RecordingProPresenter(const Config &config)
        : ProPresenterClient(config)
    {
    }

    void ensureMessage() override { calls << QStringLiteral("ensure"); }
    void setNumber(const QString &number) override
    {
        calls << QStringLiteral("set:%1").arg(number);
        setNumbers << number;
    }
    void trigger() override { calls << QStringLiteral("trigger"); }
    void clear() override { calls << QStringLiteral("clear"); }

    QStringList calls;
    QStringList setNumbers;
};

// Millisecond timings so the event-loop-driven tests stay fast.
constexpr int kBatchWaitMs = 40;
constexpr int kExpireMs = 60;
constexpr int kBatchMax = 3;

} // namespace

class TestPagerController : public QObject
{
    Q_OBJECT

private:
    // A Config the recording client can hold a reference to; never loaded, so
    // it touches no real files.
    Config m_config{QStringLiteral("/dev/null")};

private slots:
    // --- formatBatch (pure, ports process_number_batch) -------------------

    void formatBatch_singleNumberIsItself()
    {
        PagerController::Batch b{{"m1", "1234"}};
        QCOMPARE(PagerController::formatBatch(b), QString("1234"));
    }

    void formatBatch_multipleJoinsWithCommaAndAmpersand()
    {
        PagerController::Batch b{{"m1", "142"}, {"m2", "143"}, {"m3", "144"}};
        QCOMPARE(PagerController::formatBatch(b), QString("142, 143 & 144"));
    }

    void formatBatch_twoNumbersUsesAmpersandOnly()
    {
        PagerController::Batch b{{"m1", "10"}, {"m2", "20"}};
        QCOMPARE(PagerController::formatBatch(b), QString("10 & 20"));
    }

    // --- startup clear (Decision 5) ---------------------------------------

    void start_clearsOwnMessageBeforeAnyDisplay()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);

        controller.start();

        // The startup clear happens before anything is ever shown.
        QVERIFY(!client.calls.isEmpty());
        QCOMPARE(client.calls.first(), QString("ensure"));
        QVERIFY(!client.calls.contains(QStringLiteral("trigger")));
        QVERIFY(controller.activeFormatted().isEmpty());
    }

    // --- batching within the window ---------------------------------------

    void enqueue_combinesNumbersWithinWindowIntoOneBatch()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);
        controller.start();

        controller.enqueueNumber("m1", "142");
        controller.enqueueNumber("m2", "143");
        controller.enqueueNumber("m3", "144");

        // Nothing displayed until the batch window fires.
        QVERIFY(controller.activeFormatted().isEmpty());

        QTRY_COMPARE(controller.activeFormatted(), QString("142, 143 & 144"));
        QCOMPARE(client.setNumbers, QStringList{QStringLiteral("142, 143 & 144")});
    }

    // --- overflow beyond batch-max-count spills to the queue --------------

    void enqueue_overflowSpillsExcessIntoQueue()
    {
        // A long expiry keeps the first batch on screen so the queued-overflow
        // state is stable and observable (rather than a race against expiry).
        constexpr int longExpireMs = 400;
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, longExpireMs);
        controller.start();

        // Five numbers, max 3: first 3 shown, remaining 2 queued behind.
        controller.enqueueNumber("m1", "1");
        controller.enqueueNumber("m2", "2");
        controller.enqueueNumber("m3", "3");
        controller.enqueueNumber("m4", "4");
        controller.enqueueNumber("m5", "5");

        QTRY_COMPARE(controller.activeFormatted(), QString("1, 2 & 3"));
        // The excess is waiting in the queue, not on screen.
        QTRY_COMPARE(controller.queuedCount(), 1);
        QCOMPARE(controller.queuedBatches().first(),
                 (PagerController::Batch{{"m4", "4"}, {"m5", "5"}}));

        // After the first batch expires, the overflow batch shows automatically.
        QTRY_COMPARE(controller.activeFormatted(), QString("4 & 5"));
        QCOMPARE(controller.queuedCount(), 0);
    }

    // --- full send cycle: set -> trigger -> clear, then next batch --------

    void sendCycle_setTriggerClearInOrderThenNextBatch()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);
        controller.start();

        controller.enqueueNumber("m1", "111");
        QTRY_COMPARE(controller.activeFormatted(), QString("111"));

        // On send: set then trigger; expiry then clears.
        QTRY_VERIFY(client.calls.contains(QStringLiteral("clear")));

        const int set = client.calls.indexOf(QStringLiteral("set:111"));
        const int trig = client.calls.indexOf(QStringLiteral("trigger"), set);
        const int clr = client.calls.indexOf(QStringLiteral("clear"), trig);
        QVERIFY(set >= 0);
        QVERIFY(trig > set);
        QVERIFY(clr > trig);

        // Back to Idle after expiry.
        QTRY_VERIFY(controller.activeFormatted().isEmpty());
        QVERIFY(!controller.isShowing());
    }

    // --- feedback signals fire per source id, in queued/onScreen/cleared order

    void signals_fireInOrderKeyedToSourceIds()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);
        controller.start();

        QSignalSpy queuedSpy(&controller, &PagerController::queued);
        QSignalSpy onScreenSpy(&controller, &PagerController::onScreen);
        QSignalSpy clearedSpy(&controller, &PagerController::cleared);

        controller.enqueueNumber("msg-A", "100");

        // queued fires immediately on enqueue.
        QCOMPARE(queuedSpy.count(), 1);
        QCOMPARE(queuedSpy.at(0).at(0).toString(), QString("msg-A"));

        // onScreen fires on send, cleared on expiry — both keyed to msg-A.
        QTRY_COMPARE(onScreenSpy.count(), 1);
        QCOMPARE(onScreenSpy.at(0).at(0).toString(), QString("msg-A"));

        QTRY_COMPARE(clearedSpy.count(), 1);
        QCOMPARE(clearedSpy.at(0).at(0).toString(), QString("msg-A"));
    }

    // --- last_number accessor ---------------------------------------------

    void lastNumber_tracksMostRecentEnqueued()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);
        controller.start();

        QVERIFY(controller.lastNumber().isEmpty());
        controller.enqueueNumber("m1", "555");
        QCOMPARE(controller.lastNumber(), QString("555"));
        controller.enqueueNumber("m2", "777");
        QCOMPARE(controller.lastNumber(), QString("777"));
    }

    // --- queuedBusy: the hourglass condition (Task 001-7) ------------------
    //
    // Ports bot.py's `qsize()>0 or _current_nonce or len(current_batch)>=max`
    // check that decides whether an inbound number gets a ⌛ hourglass. It is
    // evaluated at enqueue time, BEFORE the number joins the batch.

    // First number into an idle, empty controller is NOT busy — no hourglass.
    void queuedBusy_silentForFirstIdleEnqueue()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);
        controller.start();

        QSignalSpy busySpy(&controller, &PagerController::queuedBusy);
        controller.enqueueNumber("m1", "1234");
        QCOMPARE(busySpy.count(), 0);
    }

    // A number arriving while a batch is on screen IS busy — hourglass fires.
    void queuedBusy_firesWhileShowing()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax, kExpireMs);
        controller.start();

        controller.enqueueNumber("m1", "111");
        QTRY_VERIFY(controller.isShowing()); // first batch now displayed

        QSignalSpy busySpy(&controller, &PagerController::queuedBusy);
        controller.enqueueNumber("m2", "222");
        QCOMPARE(busySpy.count(), 1);
        QCOMPARE(busySpy.at(0).at(0).toString(), QString("m2"));
    }

    // Once the forming batch is already at batch-max, the next number is busy.
    void queuedBusy_firesWhenCurrentBatchFull()
    {
        RecordingProPresenter client(m_config);
        // Long window so the batch keeps forming and does not flush mid-test.
        PagerController controller(&client, /*batchWaitMs=*/1000, kBatchMax,
                                   kExpireMs);
        controller.start();

        QSignalSpy busySpy(&controller, &PagerController::queuedBusy);
        // kBatchMax==3: the first three are not busy (sizes 0,1,2 before add);
        // the fourth sees a full forming batch (size 3 >= max) and hourglasses.
        controller.enqueueNumber("m1", "1");
        controller.enqueueNumber("m2", "2");
        controller.enqueueNumber("m3", "3");
        QCOMPARE(busySpy.count(), 0);
        controller.enqueueNumber("m4", "4");
        QCOMPARE(busySpy.count(), 1);
        QCOMPARE(busySpy.at(0).at(0).toString(), QString("m4"));
    }

    // --- cancel(): clear the on-screen message on demand (Task 001-7) ------

    // cancel() while showing clears the message, signals cleared, returns Idle.
    void cancel_clearsShowingMessage()
    {
        RecordingProPresenter client(m_config);
        // Long expiry so the message would otherwise stay up: cancel must be
        // what ends it, not the timer.
        PagerController controller(&client, kBatchWaitMs, kBatchMax,
                                   /*expireMs=*/5000);
        controller.start();

        controller.enqueueNumber("m1", "999");
        QTRY_VERIFY(controller.isShowing());

        QSignalSpy clearedSpy(&controller, &PagerController::cleared);
        client.calls.clear();
        controller.cancel();

        QCOMPARE(clearedSpy.count(), 1);
        QCOMPARE(clearedSpy.at(0).at(0).toString(), QString("m1"));
        QVERIFY(client.calls.contains(QStringLiteral("clear")));
        QVERIFY(!controller.isShowing());
        QVERIFY(controller.activeFormatted().isEmpty());
    }

    // After cancel, a queued batch behind the cleared one shows automatically.
    void cancel_advancesToNextQueuedBatch()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax,
                                   /*expireMs=*/5000);
        controller.start();

        // First batch shows; overflow (max 3) leaves a second batch queued.
        controller.enqueueNumber("m1", "1");
        controller.enqueueNumber("m2", "2");
        controller.enqueueNumber("m3", "3");
        controller.enqueueNumber("m4", "4");
        QTRY_COMPARE(controller.activeFormatted(), QString("1, 2 & 3"));
        QTRY_COMPARE(controller.queuedCount(), 1);

        controller.cancel();
        QTRY_COMPARE(controller.activeFormatted(), QString("4"));
        QCOMPARE(controller.queuedCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestPagerController)
#include "tst_pagercontroller.moc"
