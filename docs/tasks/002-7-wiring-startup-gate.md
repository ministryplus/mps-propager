# Task 002-7: Wiring & Startup Validation Gate (main.cpp)

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Status:** Complete
**Parallel group:** Wave 4 (solo — sole editor of main.cpp; final integration)
**Depends on:** [002-1](002-1-config-api.md), [002-2](002-2-controller-live-reload.md), [002-3](002-3-reconnect-entry-points.md), [002-5](002-5-mainwindow-tab-banner.md), [002-6](002-6-tray-reconnect-warning.md)
**Blocks:** — (none)

## What

Wire every piece from Tasks 002-1 through 002-6 together in `src/main.cpp`. This
is the final integration — the whole feature (issues **#1** reconnect and **#5**
config/validation) goes live only here. Nothing before this task edited
`main.cpp`; all of it added unused-until-wired API. This task connects the
Connections tab's `saveRequested` / `reconnectRequested` / `testRequested`
signals, the tray's `reconnectRequested` signal, and the startup validation gate
to the real objects already constructed in `main.cpp`: `Config config`,
`ProPresenterClient proPresenter(config)`, `PagerController pager(&proPresenter,
…)`, `SlackClient slack(config, &pager)`, `MainWindow window(…)`, and
`TrayMenu tray(&pager)`.

The load-bearing rule (Spec 002 Decisions 3, 6, 7): **Save, Reconnect, and the
startup gate are the same `validate → (write) → (reconnect-if-allowed)` path with
different inputs — implement it once, not three times.** Reconnect is Save with
no field changes; the startup gate is that same gate run at launch. The
dirty-tracking gate (Decision 5) is what keeps an `expire-time`-only Save from
reconnecting ProPresenter and wiping a number currently on screen.

## Steps

1. **Update `MainWindow` construction** for its new signature from Task 002-5:
   pass the `Config` (`&config` / `config`) alongside the existing `pager` and
   `config.configDir()` so the window can host the `ConnectionsTab`, render the
   validation banner, and offer Reveal Config File. No other constructor changes.

2. **Define one shared apply-section helper** (a lambda or small local function)
   capturing `config`, `slack`, `proPresenter`, `pager`, `window`, `tray`. Given a
   section (`Slack` | `ProPresenter`) and the tab's current field values + dirty
   flags, it performs the full path below. Save, Reconnect, and the startup gate
   all route through it (with the appropriate inputs) — do **not** duplicate the
   logic.

3. **Save (per section)** — connect the tab's `saveRequested(section)`:
   a. Run `Config::validate()` (Task 002-1) to get the fresh `ValidationResult`.
   b. **Always** write the section's fields via `Config::setValue`-per-key +
      `sync()`, then `Config::reload()` — even when a required field is unset, so
      the operator's typing is never lost (Decision 6, required-but-unset tier:
      *write, don't reconnect*).
   c. **Reconnect only if** at least one **connection** field for that section
      actually changed (the tab's dirty accessors from Task 002-4, per the
      Decision 4 taxonomy) **AND** that section has no required-but-unset error:
      Slack → `SlackClient::reconnectNow()` (Task 002-3); ProPresenter →
      `ProPresenterClient::reconnect()` (Task 002-3).
   d. **Behavior**-field changes (Slack `ignore-numbers`; ProPresenter
      `batch-wait-time` / `batch-max-count` / `expire-time`) reload into
      `PagerController` via the Task 002-2 setters — `setBatchWaitMs(seconds*1000)`,
      `setBatchMaxCount(count)`, `setExpireMs(seconds*1000)` (Config stores seconds;
      the timers use ms) — with **no** socket churn.
   e. Push the fresh `ValidationResult` to the tab (inline field errors/warnings),
      the `MainWindow` banner, and the `TrayMenu` warning state (Decision 8).

   **CRITICAL — Decision 5 footgun:** step (c)'s dirty-and-not-required gate is
   the whole reason an `expire-time`-only Save leaves an on-screen number intact.
   A ProPresenter reconnect re-runs `ensureMessage()`, which clears ProPager's own
   message. If Save reconnected unconditionally, editing a harmless timing value
   would wipe a live number. Only a changed **connection** field (host/port for
   PP; tokens/channel for Slack) may trigger a reconnect.

4. **Reconnect button (per section)** — connect the tab's
   `reconnectRequested(section)` to the same shared helper with **no field
   changes**: it force-reconnects that one client from last-saved config
   (`reconnectNow()` / `reconnect()`), still respecting the required-but-unset
   gate. This is Save-with-nothing-dirty (Decision 3).

5. **Tray Reconnect** — connect `TrayMenu::reconnectRequested()` (Task 002-6,
   Decision 12) to force-reconnect **both** clients from last-saved config:
   `slack.reconnectNow()` **and** `proPresenter.reconnect()` (each still gated on
   its section not having a required-but-unset error). No editing happens from the
   tray; this is the coarse "kick it without opening the window."

6. **ProPresenter Test** — connect the tab's `testRequested()` (Decision 10) to
   `ProPresenterClient::test()` (Task 002-3) and route the structured result back
   to the tab for display. Reachability + adopt/create only; it is built as the
   seam #2's deep preflight extends later, so surface the structured result
   verbatim rather than reducing it to a bool.

7. **Startup validation gate** — before the existing `slack.start()` call, run the
   **same** `Config::validate()`. If the required-but-unset Slack keys
   (`slack/bot-token`, `slack/app-token`, `slack/listen-channel`) are missing,
   **do not** call `slack.start()` — this kills the first-run
   `apps.connections.open returned no URL:` backoff spam (#5). Instead immediately
   surface the banner + tray warning ("set your Slack tokens"). ProPresenter's
   `host`/`port` are optional-with-default (Spec 001 defaults `127.0.0.1` /
   `55184`), so `proPresenter` may still `ensureMessage()` at launch. Keep the
   existing `pager.start()` (startup clear, Decision 5). The gate rule here is
   identical to the Save reconnect gate in step 3c — one rule, reused.

8. **Route validation to the cross-cutting surfaces on load** (Decision 8): run
   `validate()` once at startup (the same call as step 7) and push its result to
   the banner + tray warning, so a first launch with empty config shows the
   warning without opening the tab. After every Save (step 3e) the surfaces
   refresh from the new result and clear once the config is valid.

## Acceptance Criteria

- [ ] `src/main.cpp` is the **only** file this task modifies.
- [ ] `MainWindow` is constructed with the `Config` it needs for the Connections
      tab, banner, and Reveal Config File (matches the Task 002-5 signature).
- [ ] Save always writes + `sync()` + `reload()`s the section's fields, even when
      a required field is blank (typing is never discarded).
- [ ] Save reconnects a client **only** when a connection field for that section
      changed **and** that section has no required-but-unset error; behavior-only
      edits reload the controller via setters with no reconnect.
- [ ] An `expire-time`-only Save performs no ProPresenter reconnect (verified: a
      number on screen at Save time stays on screen).
- [ ] The per-section Reconnect button force-reconnects that one client from saved
      config via the same shared path (no field changes).
- [ ] Tray Reconnect force-reconnects **both** clients (Slack backoff reset via
      `reconnectNow()`; PP via `reconnect()`).
- [ ] The Test button calls `ProPresenterClient::test()` and the structured result
      is displayed on the tab.
- [ ] Startup gate: with required Slack keys unset, `slack.start()` is **not**
      called and no `apps.connections.open returned no URL:` line appears in the
      Log; the banner + tray warning appear immediately. `pager.start()` still runs.
- [ ] Validation results reach the banner + tray both at load and after every Save;
      the banner clears once config is valid.
- [ ] Save, Reconnect, and the startup gate share one implementation (one helper),
      not three copies.

## Files Changed

| File | Action |
|---|---|
| src/main.cpp | Modify |

## Verification

Build the app, then walk the spec's 10 numbered Verification steps end-to-end:

| # | Action | Expected |
|---|---|---|
| 1 | First run, empty config | Banner + tray warning "set your Slack tokens"; **no** `apps.connections.open` backoff spam in the Log tab. |
| 2 | Enter valid Slack `bot-token` + `app-token` + `listen-channel`, Save | Slack connects; status flips to Connected on the Overview status bar and the tray. |
| 3 | Edit `expire-time` only, Save while a number is on screen | The on-screen number **stays** (no reconnect, no clear); the new duration applies to the next display. |
| 4 | Edit `host`/`port`, Save | ProPresenter reconnects; PP status label updates. |
| 5 | Typo `bot-token` (missing `xoxb-`), Save | Inline shape warning, but Save still writes and attempts the reconnect. |
| 6 | Blank a required field, Save | Inline error + banner; **no** reconnect; typed values in the other fields are preserved. |
| 7 | Toggle reveal on a token field | Token text shows/hides. |
| 8 | Tray Reconnect while disconnected | Both clients re-attempt immediately (Slack backoff reset to base). |
| 9 | Reveal Config File | Opens the `.ini`'s location; the config-path label matches `Config::configPath()`. |
| 10 | Inspect the `.ini` after any in-app Save | Bare `key=value` (template comments gone) — expected per Decision 2. |

Additionally confirm the single-code-path property: a Reconnect button press and a
Save-with-no-changes take the same branch as the startup gate (e.g. by observing
identical reconnect behavior and the same required-but-unset short-circuit).
