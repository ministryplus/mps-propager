# Task 001-4: Pager Controller (queue, batching, expiry, state)

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Complete
**Parallel group:** Wave 4 (solo — integrates both clients)
**Depends on:** [001-3](001-3-propresenter-client.md), [001-5](001-5-slack-client.md)
**Blocks:** [001-6](001-6-ui.md), [001-7](001-7-commands-edge-cases.md)

## What

Create the `PagerController`, the coordinating core that ports the current app's batching/queue/display behavior (`bot.py`: `add_to_queue`, `task_add_batch_to_queue`, `process_number_batch`, `task_send_numbers`, `pro7_send_waiter`) onto a single Qt event loop. All timing runs on `QTimer` — a batch-window (debounce) timer and an expiry (display-duration) timer — with a `QQueue<Batch>` for FIFO overflow (Decision 7: no threads, no asyncio, no cross-thread signals). Because Pro7 gives no feedback and ProPager owns its own message lifecycle (Decision 5), the old event/nonce machinery collapses to a two-state machine, `Idle | Showing(until T)`. The controller drives the ProPresenter client (set token → trigger → clear per-message) and emits per-source-message-id state-transition signals so the Slack layer can render emoji feedback (Decision 6). This task unit-drives entirely with fake numbers — no Slack yet.

## Steps

1. Create `src/pagercontroller.h` / `src/pagercontroller.cpp`. Declare a `PagerController : QObject` that takes a reference/pointer to the `ProPresenterClient` (Task 3) in its constructor, plus the batching config values (`batch-wait-time`, `batch-max-count`, `expire-time`) from the config layer (Task 2).
2. Define the state machine as an enum reducing to `Idle | Showing`, tracking the "until T" expiry deadline. This **replaces** the removed `bot.py` machinery: `available` (`SetUnsetEvent`), `DoubleEvent`, `_current_nonce`, and the `pending{}` dict — none of these are ported; state transitions do their job.
3. Define a `Batch` type: an ordered list of `(msgId, number)` pairs. Keep a "current batch" being accumulated, the `QQueue<Batch>` of ready-but-waiting batches, and `last_number`.
4. Implement the input slot `enqueueNumber(msgId, number)` (ports `add_to_queue`): append to the current batch; if the batch-window `QTimer` is not already running, start it for `batch-wait-time` (single-shot / debounce). Emit `queued(msgId)` for feedback.
5. Implement the batch-window timeout handler (ports `task_add_batch_to_queue`): when the window fires, slice the current batch to at most `batch-max-count` into one `Batch`; any overflow beyond the max stays as a new current batch and re-arms the batch-window timer. Push the finished `Batch` onto the `QQueue`, then attempt a send.
6. Implement `formatBatch(batch)` (ports `process_number_batch`): one number → the number itself; multiple → comma-join all but the last, then ` & ` before the last (e.g. `"142, 143 & 144"`).
7. Implement the send cycle (ports `task_send_numbers` + `pro7_send_waiter`, but timer-driven): if state is `Idle` and the queue is non-empty, pop a `Batch`, transition to `Showing`, format it, call the ProPresenter client `PUT` (set token) then `trigger`, store the active formatted string, emit `onScreen(msgId)` for each id in the batch, and start the expiry `QTimer` for `expire-time` (single-shot).
8. Implement the expiry timeout handler: call the ProPresenter client per-message `clear`, emit `cleared(msgId)` for each id in the just-shown batch, transition back to `Idle`, clear the active formatted string, then attempt a send again (pops the next queued `Batch` if any).
9. Implement startup clear (Decision 5): expose a `start()`/init step that tells the ProPresenter client to clear ProPager's own message so launch begins in a known-empty state.
10. Declare the signals `queued(QString msgId)`, `onScreen(QString msgId)`, `cleared(QString msgId)` and the UI accessors: current active formatted string, the queued batches (count/contents), and `last_number`. These replace the old `DoubleEvent.wait()` / `wait_secondary()` feedback path.
11. Wire the controller in `src/main.cpp` (construct with the ProPresenter client, call startup clear) and add `src/pagercontroller.cpp` to the target sources in `CMakeLists.txt`.

## Acceptance Criteria

- [x] `src/pagercontroller.{h,cpp}` exist; `PagerController` is a `QObject` holding a `QQueue<Batch>`, a batch-window `QTimer`, and an expiry `QTimer`.
- [x] State is exactly `Idle | Showing(until T)`; no `SetUnsetEvent`, `DoubleEvent`, `_current_nonce`, or `pending{}` equivalents exist anywhere.
- [x] A number arriving via `enqueueNumber` starts/extends the batch window; numbers within `batch-wait-time` combine into one batch up to `batch-max-count`; overflow spills into the FIFO `QQueue`.
- [x] `formatBatch` returns the single number unchanged, and multiples as `"a, b & c"` (comma-join all but last, ` & ` before the last).
- [x] On send: `Idle`→`Showing`, ProPresenter `PUT` then `trigger` called, expiry `QTimer(expire-time)` started; on expiry: per-message `clear` called, `Showing`→`Idle`, next queued batch pops automatically.
- [x] Startup clears ProPager's own message before any batch is shown.
- [x] `queued` / `onScreen` / `cleared` signals fire per source message id, in that order.
- [x] UI accessors expose the current active formatted string, the queued batches, and the last number.
- [x] No `QThread` and no `std::thread` are created — everything runs on the Qt event loop.

## Files Changed

| File | Action |
|---|---|
| src/pagercontroller.h | Create |
| src/pagercontroller.cpp | Create |
| src/main.cpp | Modify |
| CMakeLists.txt | Modify |

## Verification

Unit-drive the controller with a stub/fake number source and a fake (or recording) ProPresenter client — no Slack, no live ProPresenter required. Confirm:

- Feeding several numbers within `batch-wait-time` combines them into one batch formatted as `"a, b & c"` (and a single number formats to itself).
- Feeding more than `batch-max-count` numbers overflows the excess into the `QQueue` rather than the current on-screen batch.
- A batch stays displayed for `expire-time`, then auto-clears (fake client records `PUT` → `trigger` → `clear` in order) and the next queued batch is shown automatically.
- `queued` (⌛), `onScreen` (📞), and `cleared` (👍) signals fire in order, keyed to the correct source message ids.
- Startup issues a `clear` on ProPager's own message before anything is displayed.
- Assert the whole run happens on one event loop: no `QThread` and no `std::thread` are instantiated.
