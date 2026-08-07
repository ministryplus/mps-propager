# Plan 002: Connections Tab — In-App Config, Validation & Reconnect

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Date:** 2026-08-07

## Overview

This plan breaks Spec 002 into 7 vertically-sliced tasks that fold issues **#1**
(manual reconnect, no restart) and **#5** (in-app config management + validation)
into one **Connections** tab. The strategy is *plumbing first, wire last*: the
foundational primitives (config write/reload/validate API, controller live-reload
setters, client reconnect/test entry points) land independently and leave the
app compiling and behaving exactly as before because nothing consumes them yet.
The UI surfaces (tab widget, mainwindow banner, tray warning) build on the
validation type. The final task wires everything in `main.cpp` — Save →
write → reload → *conditional* reconnect, plus the startup validation gate — and
the full feature goes live only then. This matches the spec's 7-step
Implementation Sequence and File Plan (which explicitly locates all wiring in
`main.cpp`, mirroring Spec 001's main-cpp-as-integrator pattern).

The load-bearing safety property is **Decision 5 / Decision 50** dirty-tracking:
reconnecting ProPresenter re-runs `ensureMessage()`, which clears ProPager's own
message. A blind reconnect on every Save would wipe a number currently on
screen. Reconnect is therefore gated on an actual *connection*-field change
(Decision 4 taxonomy); behavior-only edits reload silently into the controller.
That gate is the same code path used by the startup gate (Decision 7).

## Dependency Graph

```
   002-1 config-api          002-2 controller-live-reload    002-3 reconnect-entry-points
 (per-key setters,           (drop const on the 3 timing     (ProPresenterClient::reconnect()
  reload(), validate()        members; add setBatchWaitMs/     + test(); SlackClient::
  → ValidationResult)         setBatchMaxCount/setExpireMs)    reconnectNow())
        │                             │                               │
   ┌────┴──────────┐                  │                               │
   ▼               ▼                  │                               │
 002-4           002-6                │                               │
 connections-    tray-reconnect-      │                               │
 tab (widget)    warning              │                               │
   │               │                  │                               │
   ▼               │                  │                               │
 002-5             │                  │                               │
 mainwindow-       │                  │                               │
 tab-banner        │                  │                               │
   └───────────────┴──────────────────┴───────────────────────────────┘
                                   ▼
                    002-7 wiring-startup-gate  (main.cpp)
      (Save→write→reload→conditional reconnect; startup validation gate;
       tray Reconnect→both clients; validation results→banner + tray; Test)
```

## Waves (parallelism)

| Wave | Tasks | Parallel? | Rationale |
|---|---|---|---|
| 1 | `002-1` config-api, `002-2` controller-live-reload, `002-3` reconnect-entry-points | **Yes** | Disjoint files (`config.*` / `pagercontroller.*` / `propresenterclient.*`+`slackclient.*`), no data dependency. Each adds unused-until-wired API and leaves the app compiling and behaviorally unchanged. |
| 2 | `002-4` connections-tab, `002-6` tray-reconnect-warning | **Yes** | Disjoint files (`ui/connectionstab.*` new / `ui/traymenu.*`). Both consume `Config::ValidationResult` from `002-1` but nothing else; they don't touch each other. |
| 3 | `002-5` mainwindow-tab-banner | — (solo) | Adds the Connections tab into `MainWindow`'s `QTabWidget`, so it must hold a `ConnectionsTab` (depends on `002-4`) and render the banner from the validation type (`002-1`). Owns `ui/mainwindow.*`. |
| 4 | `002-7` wiring-startup-gate | — (solo) | Integrates all of the above in `main.cpp`: it is the only task that edits `main.cpp`. The full feature (and all 10 spec verification steps) goes live here. |

## Checkpoints (system stays working after each task)

- **After 002-1:** `Config` can `setValue`-per-key, `reload()`, and `validate()`
  into a structured `ValidationResult` (required-missing + shape-warnings);
  `tst_config` extended and green; app still launches (new API unused).
- **After 002-2:** the three `PagerController` timings are settable at runtime
  via setters; a mid-run change honored on the next display, no restart; app
  behavior otherwise unchanged.
- **After 002-3:** `ProPresenterClient::reconnect()` re-runs the ensure path and
  `test()` reports reachability; `SlackClient::reconnectNow()` cancels the
  backoff timer and opens immediately (backoff reset); both are safe to call
  while already connected (no duplicate sockets / leaked replies). Not yet wired.
- **After 002-4:** `ConnectionsTab` compiles and renders both per-connection
  forms (masked tokens + reveal, Save/Reconnect/Test, inline validation display,
  dirty-tracking, config-path label + Reveal Config File) standalone; not yet
  mounted in the window.
- **After 002-6:** the tray has a single **Reconnect** item and a **warning
  state** (icon/tooltip); signals are unwired until 002-7.
- **After 002-5:** `MainWindow` shows a third **Connections** tab hosting the
  widget, a dismissible-on-fix validation **banner**, and a **Reveal Config
  File** button beside Reveal Log.
- **After 002-7:** full feature live — first-run empty config shows banner + tray
  warning and does **not** start the Slack backoff loop; Save persists +
  reloads + reconnects only the dirty connection; an `expire-time`-only Save
  leaves an on-screen number intact; tray Reconnect kicks both clients.

## Task Files

| # | File | Wave | Depends on | Blocks |
|---|---|---|---|---|
| 1 | [002-1-config-api.md](002-1-config-api.md) | 1 | — | 4, 6, 7 |
| 2 | [002-2-controller-live-reload.md](002-2-controller-live-reload.md) | 1 | — | 7 |
| 3 | [002-3-reconnect-entry-points.md](002-3-reconnect-entry-points.md) | 1 | — | 7 |
| 4 | [002-4-connections-tab.md](002-4-connections-tab.md) | 2 | 1 | 5, 7 |
| 5 | [002-5-mainwindow-tab-banner.md](002-5-mainwindow-tab-banner.md) | 3 | 1, 4 | 7 |
| 6 | [002-6-tray-reconnect-warning.md](002-6-tray-reconnect-warning.md) | 2 | 1 | 7 |
| 7 | [002-7-wiring-startup-gate.md](002-7-wiring-startup-gate.md) | 4 | 1, 2, 3, 5, 6 | — |

## Notes

- **Single code path (Decisions 3, 6, 7):** Save, Reconnect, and the startup gate
  are the *same* validate → (write) → (reconnect-if-allowed) path with different
  inputs. Reconnect is Save with no field changes; the startup gate is the Save
  gate run at launch. Task 002-7 must implement this as one path, not three.
- **`.ini` becomes app-owned (Decision 2):** the first in-app Save collapses the
  commented template to bare `key=value` — accepted and expected (spec
  verification step 10). `stripInlineComment()` and the first-run template are
  **kept** in `002-1` for reading legacy/hand-edited files on load; they are not
  removed.
- **Test is reachability only (Decision 10):** `ProPresenterClient::test()`
  verifies REST reachability + message adopt/create and is built as the seam #2's
  deep preflight extends later. Its result type is structured so #2 can add
  slide-label / message-shape checks without reshaping the call site.
- **Out of scope (spec "Not Doing"):** #4 manual Clear button, #2 deep PP
  preflight, #3 setup docs / Help menu, in-app channel picker, comment-preserving
  `.ini` serializer, a separate Behavior tab, and keychain/encrypted tokens.
