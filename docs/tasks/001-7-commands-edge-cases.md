# Task 001-7: Commands & Edge Cases

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Complete
**Parallel group:** Wave 5 (parallel with [001-6](001-6-ui.md))
**Depends on:** [001-4](001-4-pager-controller.md)
**Blocks:** [001-8](001-8-build-pipeline.md)

## What

Implement the message-grammar layer that sits between `SlackClient`'s raw `messageReceived` signal (Task 5) and the `PagerController` (Task 4). This is the faithful port of the `on_message`/`sender` logic from `bot.py` (Decision 6 — the Commands and Number-match rows). The layer decides, for every inbound Slack `message` event, whether the text is a 4-digit page, a `repeat`/`cancel` command, an ignored number, or noise to drop — and it wires ProPager's own `queued`/`onScreen`/`cleared` state transitions back to Slack `reactions.add` calls for emoji feedback. The parsing lives in the `SlackClient` message handler; the command actions (`repeat`, `cancel`) become slots on the `PagerController`. **Do not edit `src/main.cpp`** — Task 6 owns UI wiring in `main.cpp`. Keep the file set to `slackclient` + `pagercontroller`.

## Steps

1. In `SlackClient`, add a message-handler slot (or extend the existing `messageReceived` dispatch) that receives `channel`, `text`, and `ts` for each `message` event already filtered to `listen-channel` (Task 5).
2. Drop any message whose `text` starts with `!` immediately — no reaction, no forward (ports `content.startswith("!")`).
3. Extract the first 4-digit run with a `QRegularExpression("(?:\\d){4}")` matched against the text (ports `re.search(r"(?:\d){4}", content)`). The first match's captured text is the number.
4. If a number matched: check it against `slack/ignore-numbers` (from config, Task 2). If present, call `reactions.add(channel, "x", ts)` (❌) and stop — do not forward.
5. Otherwise record the number as `last_number` (member on `SlackClient`) and enqueue it into the `PagerController`, passing along `channel` + `ts` so feedback reactions can be routed back to the right Slack message.
6. Wire the controller's `queued(channel, ts)` / `onScreen(channel, ts)` / `cleared(channel, ts)` signals (from Task 4) to the corresponding `reactions.add` calls: ⌛ `hourglass` on queued, 📞 `calling` on-screen, 👍 `thumbsup` on clear. The ⌛ hourglass is emitted only when the slot/queue is busy at enqueue time — port the `qsize() > 0 or showing or batch full` condition (`self.number_queue.qsize() > 0 or self._current_nonce or len(self.current_batch) >= batch-max-count`) into the controller's enqueue path so it decides whether to fire `queued`.
7. If the text contains `repeat` (substring, case-insensitive; port `"repeat" in content.lower()`): if `last_number` is set, re-enqueue it exactly as a fresh page (same feedback path); if there is no `last_number`, call `reactions.add(channel, "thumbsdown", ts)` and stop.
8. If the text contains `cancel` (substring, case-insensitive; port `"cancel" in content.lower()`): invoke the controller's `cancel()` slot to clear the currently-showing ProPager message (per-message clear via `ProPresenterClient`, Task 3), then call `reactions.add(channel, "thumbsup", ts)`.
9. Preserve the Python precedence: a 4-digit match wins over `repeat`/`cancel`; `repeat` is checked before `cancel`; anything else is ignored. `!`-prefix and channel filtering happen before all of it.
10. Add the `cancel()` slot (and, if not already present from Task 4, a `repeat`/re-enqueue path) to `PagerController`; `cancel()` clears the current message and advances the queue state (`Idle | Showing`).

## Acceptance Criteria

- [ ] Text starting with `!` is dropped with no reaction and no forward.
- [ ] The first 4-digit run in a message is extracted via `QRegularExpression("(?:\\d){4}")`.
- [ ] A number listed in `slack/ignore-numbers` gets a `x` (❌) reaction and is not forwarded.
- [ ] A fresh, non-ignored number is stored as `last_number` and enqueued into the controller with its `channel` + `ts`.
- [ ] `hourglass` (⌛) is reacted only when the slot/queue is busy at enqueue time (queue non-empty, or a message is showing, or the current batch is full); otherwise it is skipped.
- [ ] `calling` (📞) is reacted when the number goes on screen; `thumbsup` (👍) when it is cleared.
- [ ] `repeat` (case-insensitive substring) re-enqueues `last_number`; with no prior number it reacts `thumbsdown`.
- [ ] `cancel` (case-insensitive substring) clears the current ProPager message and reacts `thumbsup`.
- [ ] Non-4-digit chatter with no `repeat`/`cancel` substring is ignored entirely.
- [ ] Precedence matches `bot.py`: number match > `repeat` > `cancel`; `!`-prefix and channel filter run first.
- [ ] `src/main.cpp` is not modified; only `slackclient` and `pagercontroller` files change.

## Files Changed

| File | Action |
|---|---|
| src/slackclient.h | Modify |
| src/slackclient.cpp | Modify |
| src/pagercontroller.h | Modify |
| src/pagercontroller.cpp | Modify |

## Verification

Manual/behavioral parity against `bot.py` `on_message`/`sender`. Send each input on `listen-channel` and confirm the reaction sequence and forward behavior:

| Input (message text) | Precondition | Expected behavior | bot.py source |
|---|---|---|---|
| `1234` | slot free | Forwarded; `calling` (📞) then `thumbsup` (👍). No hourglass. | `sender`: `event.wait()` → `calling`, `wait_secondary()` → `thumbsup` |
| `1234` | slot/queue busy | `hourglass` (⌛) at enqueue, then `calling` (📞), then `thumbsup` (👍). | `sender`: `qsize()>0 or _current_nonce or batch full` → `hourglass` |
| `5555` | `5555` in `slack/ignore-numbers` | `x` (❌) reaction; **not** forwarded. | `if num in ignore-numbers: reactions_add "x"; return` |
| `!note 1234` | any | Ignored entirely — no reaction, no forward (even though it contains `1234`). | `if content.startswith("!"): return` |
| `repeat` | `last_number` set | Re-sends `last_number` through the normal forward + feedback path. | `elif "repeat": if last_number: await sender(last_number)` |
| `repeat` | no prior number | `thumbsdown` reaction; nothing forwarded. | `else: reactions_add "thumbsdown"` |
| `cancel` | any | Current ProPager message cleared; `thumbsup` (👍) reaction. | `elif "cancel": propres_cancel_number(); reactions_add "thumbsup"` |
| `hello team` | any | Ignored entirely (no 4-digit match, no `repeat`/`cancel`). | no branch taken in `on_message` |

Confirm precedence with mixed inputs (e.g. `repeat 4321` forwards `4321`, not the last number, because the 4-digit branch is checked first).
