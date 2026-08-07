# Task 002-6: Tray Reconnect + Warning State

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Parallel group:** Wave 2 (parallel with [002-4](002-4-connections-tab.md) — disjoint files; both consume `Config::ValidationResult` from [002-1](002-1-config-api.md))
**Depends on:** [002-1](002-1-config-api.md)
**Blocks:** [002-7](002-7-wiring-startup-gate.md)

## What

Extend `TrayMenu` (`src/ui/traymenu.{h,cpp}`) with the two cross-cutting tray affordances Spec 002 requires without opening the window: a **single coarse Reconnect** action (Decision 12) and a **warning state** (Decision 8). The tray has deliberately no per-connection UI — per-connection Save/Reconnect/Test lives on the Connections tab (Task 002-4). The tray stays coarse: one `Reconnect` that (once wired in Task 002-7) force-reconnects **both** clients from last-saved config, plus one warning indicator (icon/tooltip) that is shown whenever config is invalid **or** a client is down. Following the existing `openWindowRequested()` pattern, `Reconnect` is surfaced as a **signal** (`reconnectRequested()`) so `main.cpp` (Task 002-7) owns the actual reconnect wiring — this class stays pure UI. This satisfies #1's "kick it without opening the window" and #5's "config problems visible without opening the tab."

## Steps

1. In `src/ui/traymenu.h`, add a new `QAction *m_reconnectAction` member and a `void reconnectRequested()` signal. In the constructor (`src/ui/traymenu.cpp`), insert a **Reconnect** action into `m_menu` — placed with the actionable items (near **Open Window**, above **Quit**, after the disabled status/active lines), so the menu reads: ProPresenter status, Slack status, sep, active-number line, sep, **Reconnect**, **Open Window**, **Quit**. Connect the action's `triggered` to emit `reconnectRequested()`. Do **not** perform any reconnect here — `main.cpp` connects `reconnectRequested()` to both clients' reconnect entry points (Task 002-3) in Task 002-7.
2. Track the three inputs that determine the warning state as members: config validity (from the validation slot below) and the two client-connected booleans already fed by `setSlackConnected`/`setProPresConnected`. Add `bool m_configValid = true;`, and reuse/introduce `bool m_slackConnected = false;` / `bool m_proPresConnected = false;` to mirror the existing status-line state.
3. Add a public slot to receive the config-validation state, e.g. `void setConfigValidation(const Config::ValidationResult &result)` (type from Task 002-1). Store `m_configValid = result.ok()` (i.e. no required-but-unset keys — present-but-malformed *warnings* do not block, matching Decision 6, but see the precedence note in step 5 for how they surface) and keep the human-readable summary for the tooltip. Include `"config.h"` in `traymenu.cpp`.
4. Update `setSlackConnected` / `setProPresConnected` to store `m_slackConnected` / `m_proPresConnected` (in addition to their existing status-line text updates), then call a private `refreshWarningState()`.
5. Implement `refreshWarningState()` — the single place that reconciles all inputs into the tray's warning indicator. **Precedence:** the tray shows a **warning** whenever `!m_configValid` (a required key is unset → cannot connect) **or** `!m_slackConnected` **or** `!m_proPresConnected`; it shows the **normal** state only when config is valid **and** both clients are connected. When warning, set a warning icon variant and a `QSystemTrayIcon::setToolTip()` describing the reason, in this priority order: (a) required config missing (from the validation summary — most actionable, "fill in X to connect"), else (b) a client down ("Slack disconnected" / "ProPresenter disconnected" / both). Present-but-malformed *warnings* alone (config still `ok()`, both clients up) do **not** trip the warning icon — they are surfaced inline on the tab and in the window banner (Task 002-5); document this so the tray isn't left permanently yellow over a cosmetic shape warning.
6. Keep the tray icon assets simple: reuse the existing tray icon for the normal state and derive a warning variant (e.g. an overlaid badge, an alternate `.icns`/resource, or a distinct `QIcon`), and set `setToolTip()` regardless so the reason is available on hover even if the icon delta is subtle. Do not add per-connection menu items — the tray stays coarse (Decision 12).

## Acceptance Criteria

- [ ] `src/ui/traymenu.h` declares a `reconnectRequested()` signal, a `setConfigValidation(const Config::ValidationResult&)` slot, and the `m_reconnectAction` / `m_configValid` / `m_slackConnected` / `m_proPresConnected` members.
- [ ] The tray menu contains a single **Reconnect** action (above **Quit**); triggering it emits `reconnectRequested()` and performs no reconnect itself (wiring is Task 002-7).
- [ ] There is exactly one Reconnect in the tray — no per-connection reconnect/test items (Decision 12).
- [ ] A warning icon/tooltip appears whenever config is invalid (required key unset) **or** either client is disconnected; it clears to the normal state only when config is valid **and** both clients are connected.
- [ ] The warning tooltip states the reason with the documented precedence (missing-config message first, else which client is down).
- [ ] A present-but-malformed-only condition (config `ok()`, both clients up) does **not** trip the tray warning (it is surfaced on the tab/banner instead), per the documented precedence.
- [ ] `setSlackConnected` / `setProPresConnected` still update the existing status-line actions **and** now feed `refreshWarningState()`.
- [ ] The class remains pure UI/signal — no reconnect logic, no direct client references beyond the existing `PagerController *m_pager`.

## Files Changed

| File | Action |
|---|---|
| src/ui/traymenu.h | Modify |
| src/ui/traymenu.cpp | Modify |

## Verification

1. Build the app (`build` per project conventions) — `traymenu.cpp` includes `config.h` and compiles against the Task 002-1 `ValidationResult` type.
2. Launch with an **empty/invalid** config: the tray shows the **warning** icon and its tooltip names the missing config ("fill in your Slack tokens…"), before any connection attempt.
3. Open the tray menu: confirm a single **Reconnect** item sits above **Quit** and there are no per-connection items. Connect a slot to `reconnectRequested()` (or observe in Task 002-7) and confirm choosing **Reconnect** emits the signal exactly once.
4. With valid config, drive `setConfigValidation(valid)` plus `setSlackConnected(true)` / `setProPresConnected(true)`: the warning clears to the normal icon/tooltip.
5. Emit `setSlackConnected(false)` (or `setProPresConnected(false)`): the warning re-appears with a "…disconnected" tooltip; restore both → warning clears again. Confirm all transitions happen live, no restart.
6. Feed a present-but-malformed-only validation result (config `ok()`, both clients up): confirm the tray stays in the **normal** state (no false warning), matching the documented precedence.
