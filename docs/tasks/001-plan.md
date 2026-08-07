# Plan 001: ProPager — Native Qt6/C++ Rewrite

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Pending
**Date:** 2026-08-06

## Overview

This plan breaks Spec 001 into 8 vertically-sliced tasks. Each task leaves the
project in a compiling, runnable state. The rewrite replaces the entire Python
app (`bot.py`, `main.py`, `ui/*.py`, `server.py`) with a single-process,
single-threaded Qt6/C++ app. The two build-time TODOs from the spec are folded
into the tasks that own them (per-message clear path → Task 3; config path →
Task 2).

## Dependency Graph

```
                     001-1 skeleton
                          │              (CMake, main.cpp, tray icon, identity)
                          ▼
                     001-2 config
                          │              (QSettings IniFormat, path resolution)
                ┌─────────┴──────────┐
                ▼                    ▼
       001-3 propresenter     001-5 slack-client      ◀── Wave 3 (parallel)
         (REST /v1 client)      (Socket Mode + Web API)
                └─────────┬──────────┘
                          ▼
                   001-4 pager-controller               ◀── Wave 4
                     (queue, batch, expiry, state,
                      wires Slack events → PP + feedback)
                ┌─────────┴──────────┐
                ▼                    ▼
          001-6 ui             001-7 commands           ◀── Wave 5 (parallel)
        (status window,       (repeat / cancel /
         tray menu,            ignore-numbers,
         signal-slot)          !-prefix, 4-digit)
                └─────────┬──────────┘
                          ▼
                   001-8 build-pipeline                  ◀── Wave 6
                     (deploy, sign, notarize,
                      staple, .dmg)
```

## Waves (parallelism)

| Wave | Tasks | Can run in parallel? | Rationale |
|---|---|---|---|
| 1 | `001-1` skeleton | — (solo) | Establishes CMake project + file layout; blocks everything. |
| 2 | `001-2` config | — (solo) | Central `Config` module every client reads; blocks the clients. |
| 3 | `001-3` propresenter-client, `001-5` slack-client | **Yes** | Independent modules, disjoint files, no shared state — they only meet later at the controller's interface. |
| 4 | `001-4` pager-controller | — (solo) | Integrates both clients: consumes Slack numbers, drives the PP client, emits state transitions for feedback. |
| 5 | `001-6` ui, `001-7` commands | **Yes** | Disjoint files: UI owns `src/ui/*` + tray/status wiring; commands own Slack message parsing + controller command slots. `main.cpp` wiring is owned by Task 6 only. |
| 6 | `001-8` build-pipeline | — (solo) | Needs the full app compiling; produces the signed/notarized `.dmg`. |

## Checkpoints (system stays working after each task)

- **After 001-1:** app launches, shows a tray icon, quits cleanly.
- **After 001-2:** app reads/writes config; logs the resolved on-disk path (TODO 2 closed).
- **After 001-3:** against a live ProPresenter, can ensure-message, set token, trigger, and per-message clear (TODO 1 closed).
- **After 001-4:** fake numbers fed in batch/queue/expire correctly and drive the PP client end-to-end (no Slack yet).
- **After 001-5:** connects to Slack Socket Mode, acks envelopes, receives `message` events, survives a socket drop (reconnect).
- **After 001-6:** status window + tray menu reflect live connection state and active/queued numbers via signals (no polling thread).
- **After 001-7:** `repeat`, `cancel`, `ignore-numbers`, `!`-prefix, and 4-digit extraction all behave at parity with `bot.py`.
- **After 001-8:** a notarized, stapled `.dmg` launches clean on a second Mac with no prerequisites.

## Task Files

| # | File | Wave | Depends on | Blocks |
|---|---|---|---|---|
| 1 | [001-1-project-skeleton.md](001-1-project-skeleton.md) ✅ | 1 | — | all |
| 2 | [001-2-config-layer.md](001-2-config-layer.md) ✅ | 2 | 1 | 3, 5 |
| 3 | [001-3-propresenter-client.md](001-3-propresenter-client.md) ✅ | 3 | 2 | 4 |
| 4 | [001-4-pager-controller.md](001-4-pager-controller.md) | 4 | 3, 5 | 6, 7 |
| 5 | [001-5-slack-client.md](001-5-slack-client.md) ✅ | 3 | 2 | 4 |
| 6 | [001-6-ui.md](001-6-ui.md) | 5 | 4 | 8 |
| 7 | [001-7-commands-edge-cases.md](001-7-commands-edge-cases.md) | 5 | 4 | 8 |
| 8 | [001-8-build-pipeline.md](001-8-build-pipeline.md) | 6 | 6, 7 | — |

## Notes

- **Parity source of truth:** the current behavior lives in `bot.py` (batching,
  `process_number_batch` formatting, emoji sequence, command grammar) and
  `ui/overview.py` (status surface). Tasks reference specific functions to port.
- **Removed wholesale:** `SetUnsetEvent` / `DoubleEvent` / `available` /
  `_current_nonce` state machine, the `/remote` WebSocket (protocol 701),
  `server.py` OAuth broker, the `~/Documents` config/log location, and the
  `poll_in_thread` status polling thread.
- **File cleanup** (delete Python originals) happens as the final step of
  Task 8, once parity is confirmed — not before, so the old app stays runnable
  as a reference during the rewrite.
