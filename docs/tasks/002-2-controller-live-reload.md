# Task 002-2: PagerController Live-Reload (behavior settings, no restart)

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Parallel group:** Wave 1 (parallel with [002-1](002-1-config-api.md), [002-3](002-3-reconnect-entry-points.md) — disjoint files)
**Depends on:** — (none)
**Blocks:** [002-7](002-7-wiring-startup-gate.md)

## What

Make the three `PagerController` timing settings re-loadable at runtime so a
behavior-field **Save** on the Connections tab applies with **no restart and no
socket churn**. Today `main.cpp` snapshots `batch-wait-time`, `batch-max-count`,
and `expire-time` into `const int` members (`m_batchWaitMs`, `m_batchMaxCount`,
`m_expireMs`) at construction — they can never change while the app runs. Per
Spec 002 Decision 9 (and Decision 4's field taxonomy, which classifies these
three keys as **behavior**, not **connection**), drop the `const` and add public
setters (`setBatchWaitMs`, `setBatchMaxCount`, `setExpireMs`) that overwrite the
members in place. Behavior edits then reload straight into the controller
instead of reconnecting a client — which is exactly what protects a live
on-screen number: an `expire-time` edit must not trigger a ProPresenter
reconnect (that reconnect would re-run `ensureMessage()` and clear the message,
Decision 5). This is the **only** genuinely new reload plumbing in Spec 002: the
client reconnects (Task 002-3) read `m_config` live and need no snapshot-setter
equivalent. This task adds the setters only; the wiring from a Save into these
setters lives in Task 002-7.

## Steps

1. In `src/pagercontroller.h`, drop the `const` qualifier from the three timing
   members so they become plain `int m_batchWaitMs;`, `int m_batchMaxCount;`,
   `int m_expireMs;`.
2. Declare three public setters on `PagerController`:
   `void setBatchWaitMs(int ms);`, `void setBatchMaxCount(int count);`,
   `void setExpireMs(int ms);`. They take **milliseconds** (Config stores
   seconds; the caller in Task 002-7 multiplies by 1000, matching the existing
   constructor call in `main.cpp`: `config.batchWaitTime() * 1000`, etc.).
   Place them near the constructor / UI-accessor block, not in `public slots`
   (they are plain reload setters, not event-loop slots).
3. Implement each setter in `src/pagercontroller.cpp` as a direct assignment to
   the corresponding member. Do **not** stop, restart, or re-arm
   `m_batchTimer` / `m_expiryTimer` inside the setters, and do **not** touch
   `m_state`, `m_queue`, `m_currentBatch`, or `m_showing`.
4. Rely on the existing single-shot timer semantics for when a new value takes
   effect: `m_batchTimer` is (re)armed with `m_batchWaitMs` inside
   `enqueueNumber`, and `m_expiryTimer` with `m_expireMs` when a batch is sent
   (`trySend`); `m_batchMaxCount` is read when a batch is sliced in
   `onBatchWindowTimeout`. Because each member is read at (re)arm/slice time, a
   new value naturally applies to the **next** batch window / display / slice —
   an in-flight display keeps its current duration until it expires. Document
   this "applies to the next cycle, not the in-flight one" semantics in a
   comment on the setters.
5. Leave the constructor signature unchanged — the initial values still arrive
   as constructor arguments from `main.cpp`; the setters only override them
   afterward.

## Acceptance Criteria

- [ ] `m_batchWaitMs`, `m_batchMaxCount`, and `m_expireMs` are declared **without**
      `const` in `src/pagercontroller.h`.
- [ ] Public setters `setBatchWaitMs(int)`, `setBatchMaxCount(int)`, and
      `setExpireMs(int)` exist, take milliseconds, and assign their member directly.
- [ ] The setters do not stop/restart/re-arm `m_batchTimer` or `m_expiryTimer`
      and do not mutate `m_state`, `m_queue`, `m_currentBatch`, or `m_showing`.
- [ ] A timing changed via a setter takes effect on the **next** batch window /
      display / slice; a display already on screen keeps its original duration.
- [ ] The constructor signature is unchanged and still seeds the three members;
      the app compiles and behaves identically until a caller invokes a setter
      (wiring lands in Task 002-7).
- [ ] A comment on the setters documents the "next cycle, not in-flight"
      semantics.

## Files Changed

| File | Action |
|---|---|
| src/pagercontroller.h | Modify |
| src/pagercontroller.cpp | Modify |

## Verification

Unit-drive the controller with the existing fake/recording ProPresenter client
(no Slack, no live ProPresenter) — extend the Task 001-4 style harness:

1. **Expiry reload applies to the next display, not the in-flight one:** start a
   display with a known `expire-time`, then call `setExpireMs()` with a new value
   while it is showing; confirm the on-screen batch clears on its *original*
   deadline (no early clear, no re-arm), and that the *next* batch honors the new
   duration.
2. **Batch-window reload:** call `setBatchWaitMs()` between batches and confirm
   the next `enqueueNumber` arms the window with the new value (the next batch
   waits the new duration).
3. **Batch-max reload:** call `setBatchMaxCount()` and confirm the next
   `onBatchWindowTimeout` slices at the new cap (overflow beyond the new max
   spills to the queue).
4. **No socket churn / no reconnect:** confirm the setters issue no
   ProPresenter `PUT`/`trigger`/`clear` and do not disturb an active display —
   this is the property that lets a behavior-only Save avoid the reconnect that
   would wipe a live number (Decision 5/9).
5. Confirm the app still builds and a no-setter run is byte-for-byte the same
   behavior as before this task (the three members simply start from their
   constructor-seeded values).
