#include "pagercontroller.h"

#include <QStringList>
#include <QTimer>

#include "propresenterclient.h"

PagerController::PagerController(ProPresenterClient *client,
                                int batchWaitMs,
                                int batchMaxCount,
                                int expireMs,
                                QObject *parent)
    : QObject(parent),
      m_client(client),
      m_batchWaitMs(batchWaitMs),
      m_batchMaxCount(batchMaxCount),
      m_expireMs(expireMs),
      m_batchTimer(new QTimer(this)),
      m_expiryTimer(new QTimer(this))
{
    m_batchTimer->setSingleShot(true);
    m_expiryTimer->setSingleShot(true);
    connect(m_batchTimer, &QTimer::timeout,
            this, &PagerController::onBatchWindowTimeout);
    connect(m_expiryTimer, &QTimer::timeout,
            this, &PagerController::onExpiryTimeout);
}

QString PagerController::formatBatch(const Batch &batch)
{
    // Ports process_number_batch: single number -> itself; multiples ->
    // comma-join all but the last, then " & " before the last.
    if (batch.isEmpty())
        return QString();
    if (batch.size() == 1)
        return batch.first().second;

    QStringList numbers;
    numbers.reserve(batch.size());
    for (const Entry &e : batch)
        numbers << e.second;

    const QString last = numbers.takeLast();
    return numbers.join(QStringLiteral(", ")) + QStringLiteral(" & ") + last;
}

QString PagerController::activeFormatted() const { return m_activeFormatted; }

QList<PagerController::Batch> PagerController::queuedBatches() const
{
    return QList<Batch>(m_queue.begin(), m_queue.end());
}

PagerController::Batch PagerController::currentBatch() const
{
    return m_currentBatch;
}

int PagerController::queuedCount() const { return m_queue.size(); }

QString PagerController::lastNumber() const { return m_lastNumber; }

bool PagerController::isShowing() const { return m_state == State::Showing; }

void PagerController::start()
{
    // Startup recovery (Decision 5): ensureMessage finds-or-creates ProPager's
    // own message and clears it, so launch begins from a known-empty state
    // before any batch is displayed.
    if (m_client)
        m_client->ensureMessage();
}

void PagerController::enqueueNumber(const QString &msgId, const QString &number)
{
    // Ports add_to_queue: append to the accumulating batch and arm the debounce
    // window on the first number (later numbers within the window do not
    // restart it). Feedback is immediate.
    //
    // Ports bot.py's hourglass check (Task 001-7): evaluated BEFORE the number
    // joins the batch, the slot is "busy" when a batch is already waiting, one
    // is on screen, or the forming batch has hit batchMaxCount. Only then does
    // the number earn a ⌛ (queuedBusy); an idle first number does not.
    const bool busy = !m_queue.isEmpty() || m_state == State::Showing ||
                      m_currentBatch.size() >= m_batchMaxCount;

    m_currentBatch.append({msgId, number});
    m_lastNumber = number;
    emit queued(msgId);
    if (busy)
        emit queuedBusy(msgId);

    if (!m_batchTimer->isActive())
        m_batchTimer->start(m_batchWaitMs);
}

void PagerController::cancel()
{
    // On-demand clear (bot.py `cancel`). If a batch is on screen, stop its
    // expiry timer and run the same teardown the timer would have — clear the
    // message, signal cleared, return to Idle, and pop the next batch. When
    // nothing is showing, still hide the message defensively (bot.py always
    // sent messageHide) without touching queue state.
    if (m_state == State::Showing) {
        m_expiryTimer->stop();
        finishCurrent();
    } else if (m_client) {
        m_client->clear();
    }
}

void PagerController::onBatchWindowTimeout()
{
    // Ports task_add_batch_to_queue: slice at most batchMaxCount into one ready
    // Batch; any overflow stays as a new current batch and re-arms the window.
    if (m_currentBatch.isEmpty())
        return;

    Batch ready;
    if (m_currentBatch.size() > m_batchMaxCount) {
        ready = m_currentBatch.mid(0, m_batchMaxCount);
        m_currentBatch = m_currentBatch.mid(m_batchMaxCount);
        m_batchTimer->start(m_batchWaitMs); // drain the overflow next window
    } else {
        ready = m_currentBatch;
        m_currentBatch.clear();
    }

    m_queue.enqueue(ready);
    trySend();
}

void PagerController::trySend()
{
    // Ports task_send_numbers: only one batch shows at a time. When Idle and a
    // batch is waiting, display it (PUT the token, then trigger) and start the
    // expiry timer. ProPager owns the lifecycle, so there is no feedback to
    // wait on — the expiry timer drives the next transition.
    if (m_state != State::Idle || m_queue.isEmpty())
        return;

    m_showing = m_queue.dequeue();
    m_state = State::Showing;
    m_activeFormatted = formatBatch(m_showing);

    if (m_client) {
        m_client->setNumber(m_activeFormatted);
        m_client->trigger();
    }

    for (const Entry &e : m_showing)
        emit onScreen(e.first);

    m_expiryTimer->start(m_expireMs);
}

void PagerController::onExpiryTimeout()
{
    // Ports the tail of pro7_send_waiter: the display duration elapsed, so tear
    // the current batch down and advance.
    finishCurrent();
}

void PagerController::finishCurrent()
{
    // Clear the message (per-message, Decision 5), signal cleared for every
    // source id, return to Idle, then pop the next queued batch if any. Shared
    // by the expiry timer (onExpiryTimeout) and on-demand cancel().
    if (m_client)
        m_client->clear();

    for (const Entry &e : m_showing)
        emit cleared(e.first);

    m_showing.clear();
    m_activeFormatted.clear();
    m_state = State::Idle;

    trySend();
}
