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

    // --- Task 002-2: live-reload timing setters ---------------------------
    //
    // A behavior-field Save reloads straight into the controller (no restart,
    // no socket churn). Each timing is read at (re)arm/slice time, so a new
    // value applies to the NEXT cycle — an in-flight display keeps its own
    // duration. These tests pin exactly that.

    // setExpireMs while a batch is showing must NOT re-arm the expiry timer:
    // the on-screen batch clears on its ORIGINAL (short) deadline, not the new
    // (long) one — this is what protects a live number from a behavior Save.
    void setExpireMs_doesNotReArmInFlightDisplay()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax,
                                   /*expireMs=*/400);
        controller.start();

        controller.enqueueNumber("m1", "111");
        QTRY_VERIFY(controller.isShowing());

        // Change to a much longer expiry mid-display.
        controller.setExpireMs(5000);

        // It still clears on the original ~400ms deadline — well under 2000ms
        // and far below the new 5000ms a buggy re-arm would impose.
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isShowing(), 2000);
    }

    // A new expire-time set while idle applies to the NEXT display.
    void setExpireMs_appliesToNextDisplay()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax,
                                   /*expireMs=*/5000);
        controller.start();

        // Shorten the display duration before anything shows.
        controller.setExpireMs(80);

        controller.enqueueNumber("m1", "111");
        QTRY_VERIFY(controller.isShowing());

        // Clears on the new 80ms duration, not the 5000ms it was built with.
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isShowing(), 2000);
    }

    // A new batch-wait-time arms the next batch window with the new value.
    void setBatchWaitMs_appliesToNextWindow()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, /*batchWaitMs=*/5000, kBatchMax,
                                   /*expireMs=*/5000);
        controller.start();

        // Shorten the debounce window before the first number arrives.
        controller.setBatchWaitMs(60);

        controller.enqueueNumber("m1", "222");
        QVERIFY(controller.activeFormatted().isEmpty()); // not immediate

        // Shows within the new 60ms window, not the 5000ms it was built with.
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeFormatted(), QString("222"),
                                  2000);
    }

    // A new batch-max-count slices the next ready batch at the new cap.
    void setBatchMaxCount_appliesToNextSlice()
    {
        RecordingProPresenter client(m_config);
        // Long expiry so the first sliced batch stays on screen and the
        // overflow is stably observable in the queue.
        PagerController controller(&client, /*batchWaitMs=*/60, kBatchMax,
                                   /*expireMs=*/5000);
        controller.start();

        // Tighten the cap from 3 to 2 before the window fires.
        controller.setBatchMaxCount(2);

        controller.enqueueNumber("m1", "1");
        controller.enqueueNumber("m2", "2");
        controller.enqueueNumber("m3", "3");

        // Sliced at the NEW max 2: "1 & 2" shows, "3" spills to the queue.
        QTRY_COMPARE(controller.activeFormatted(), QString("1 & 2"));
        QTRY_COMPARE(controller.queuedCount(), 1);
        QCOMPARE(controller.queuedBatches().first(),
                 (PagerController::Batch{{"m3", "3"}}));
    }

    // The setters issue no ProPresenter set/trigger/clear and do not disturb an
    // active display — the property that lets a behavior-only Save avoid the
    // reconnect that would wipe a live number (Decision 5/9).
    void setters_causeNoSocketChurnOrDisplayChange()
    {
        RecordingProPresenter client(m_config);
        PagerController controller(&client, kBatchWaitMs, kBatchMax,
                                   /*expireMs=*/5000);
        controller.start();

        controller.enqueueNumber("m1", "999");
        QTRY_VERIFY(controller.isShowing());
        const QString shown = controller.activeFormatted();

        client.calls.clear();
        controller.setBatchWaitMs(1234);
        controller.setBatchMaxCount(9);
        controller.setExpireMs(6789);

        // Synchronous setters: no PP calls, display untouched.
        QVERIFY(client.calls.isEmpty());
        QVERIFY(controller.isShowing());
        QCOMPARE(controller.activeFormatted(), shown);
    }
};

QTEST_GUILESS_MAIN(TestPagerController)
#include "tst_pagercontroller.moc"
