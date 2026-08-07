#ifndef PROPAGER_PAGERCONTROLLER_H
#define PROPAGER_PAGERCONTROLLER_H

#include <QList>
#include <QObject>
#include <QPair>
#include <QQueue>
#include <QString>
#include <QVector>

class ProPresenterClient;
class QTimer;

// PagerController is the coordinating core (Spec 001, Decisions 5-7). It ports
// bot.py's batching/queue/display behavior (add_to_queue,
// task_add_batch_to_queue, process_number_batch, task_send_numbers,
// pro7_send_waiter) onto a single Qt event loop — no threads, no asyncio.
//
// Timing is two single-shot QTimers: a batch-window (debounce) timer that
// gathers numbers arriving close together into one Batch, and an expiry
// (display-duration) timer that ends a display. Ready batches wait FIFO in a
// QQueue. Because ProPresenter gives no feedback and ProPager owns its own
// message lifecycle (Decision 5), the old event/nonce machinery collapses to
// a two-state machine: Idle | Showing. State transitions emit per-source-id
// signals (queued/onScreen/cleared) so the Slack layer can render emoji
// feedback (Decision 6) — this replaces the removed SetUnsetEvent/DoubleEvent/
// _current_nonce/pending machinery entirely.
class PagerController : public QObject
{
    Q_OBJECT

public:
    // One source message and the number it carried: (msgId, number).
    using Entry = QPair<QString, QString>;
    // An ordered set of entries shown together on one ProPresenter message.
    using Batch = QVector<Entry>;

    // The client drives ProPresenter; the three timings come from Config
    // (Task 002) converted to milliseconds by the caller (Config stores
    // seconds). batchMaxCount caps how many numbers combine onto one message.
    PagerController(ProPresenterClient *client,
                    int batchWaitMs,
                    int batchMaxCount,
                    int expireMs,
                    QObject *parent = nullptr);

    // Ports process_number_batch: a single number formats to itself; multiples
    // comma-join all but the last, then " & " before the last
    // (e.g. "142, 143 & 144"). Pure — unit-tested directly.
    static QString formatBatch(const Batch &batch);

    // --- UI accessors (replace the old DoubleEvent feedback path) ----------
    QString activeFormatted() const;    // on-screen string; empty when Idle
    QList<Batch> queuedBatches() const; // ready-but-waiting batches, FIFO order
    Batch currentBatch() const;         // the still-forming batch (pre-window)
    int queuedCount() const;            // number of waiting batches
    QString lastNumber() const;         // most recently enqueued number
    bool isShowing() const;             // true while a batch is displayed

public slots:
    // Startup recovery (Decision 5): clear ProPager's own message so launch
    // begins from a known-empty state before anything is displayed.
    void start();
    // Ports add_to_queue: append to the current batch, arm the batch window if
    // idle, and emit queued(msgId) for feedback.
    void enqueueNumber(const QString &msgId, const QString &number);
    // Ports bot.py's `cancel` branch (Task 001-7): clear the on-screen ProPager
    // message immediately (per-message, Decision 5) and advance to the next
    // queued batch, instead of waiting for the expiry timer. A no-op display
    // change when nothing is showing (still hides the message defensively).
    void cancel();

signals:
    void queued(const QString &msgId);   // accepted, waiting to show (UI trigger)
    void queuedBusy(const QString &msgId); // ⌛ enqueued while slot/queue busy
    void onScreen(const QString &msgId); // 📞 now displayed
    void cleared(const QString &msgId);  // 👍 display finished

private slots:
    void onBatchWindowTimeout(); // ports task_add_batch_to_queue
    void onExpiryTimeout();      // ports the tail of pro7_send_waiter

private:
    enum class State { Idle, Showing };

    void trySend();      // ports task_send_numbers: pop + display if Idle
    void finishCurrent(); // clear the showing batch, signal cleared, advance

    ProPresenterClient *m_client;
    const int m_batchWaitMs;
    const int m_batchMaxCount;
    const int m_expireMs;

    State m_state = State::Idle;
    Batch m_currentBatch;    // accumulating within the batch window
    QQueue<Batch> m_queue;   // ready batches awaiting display (FIFO overflow)
    Batch m_showing;         // the batch currently on screen
    QString m_activeFormatted;
    QString m_lastNumber;

    QTimer *m_batchTimer;
    QTimer *m_expiryTimer;
};

#endif // PROPAGER_PAGERCONTROLLER_H
