# Task 002-4: Connections Tab (per-connection form widget)

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Parallel group:** Wave 2 (parallel with [002-6](002-6-tray-reconnect-warning.md) — disjoint files; both consume `Config::ValidationResult` from 002-1)
**Depends on:** [002-1](002-1-config-api.md)
**Blocks:** [002-5](002-5-mainwindow-tab-banner.md), [002-7](002-7-wiring-startup-gate.md)

## What

Build `ConnectionsTab` (`src/ui/connectionstab.{h,cpp}`), the new `QWidget` that
is step 4 of the Implementation Sequence and the visible core of Spec 002. It is
a form over `Config`, organized into two sections — **Slack** and
**ProPresenter** — each a small form plus a **Save** control and a
**Reconnect**/**Test** control (Decision 1). Every editable key from the
Decision-4 taxonomy gets a field; connection fields are distinguished from
behavior fields so the reconnect gate (Decision 5) can key off *connection*
changes only. Tokens are masked with a reveal toggle (Decision 11). The tab
prefills from `Config`, tracks which fields changed since load/save
(dirty-tracking, Decision 5), renders the `Config::ValidationResult` inline per
field (Decision 6), and surfaces the resolved config path plus a **Reveal Config
File** affordance (Decision 13).

Consistent with Spec 001's main-cpp-as-integrator pattern, **the tab is a pure
form**: it does not write `Config`, reload the controller, or call client
reconnects itself. It emits high-level signals (`saveRequested`,
`reconnectRequested`, `testRequested`) and exposes accessors for current field
values + per-section dirty flags. Task 002-7 owns the write → reload →
*conditional* reconnect wiring in `main.cpp`. This task registers the new source
in `CMakeLists.txt` so the project compiles with the widget built, even though it
is not mounted into `MainWindow` until 002-5.

This is deliberately **not** a generic "Settings" tab; #4's manual Clear button
is explicitly out of scope (it is a pager runtime action for Overview + tray, in
its own issue).

## Steps

1. Create `src/ui/connectionstab.h` declaring `ConnectionsTab : QWidget`. The
   constructor takes a `const Config&` (for prefill + `configPath()`) and a
   `QWidget *parent = nullptr`. Define a small `enum class Section { Slack,
   ProPresenter }` used by the signals below.
2. Build the **Slack** section (a `QGroupBox`/form) with fields per Decision 4:
   - `slack/bot-token` — connection, **secret**: masked `QLineEdit`
     (`QLineEdit::Password`) with an eye toggle to reveal/hide.
   - `slack/app-token` — connection, **secret**: masked, with reveal toggle.
   - `slack/listen-channel` — connection: plain text field (kept a text field;
     no in-app channel picker in this spec).
   - `slack/ignore-numbers` — behavior: text field (comma/space-separated,
     matching how `Config` exposes the `QStringList`).
   - A **Save** button and a **Reconnect** button for the section.
3. Build the **ProPresenter** section with fields per Decision 4:
   - `propresenter/host` — connection: text field.
   - `propresenter/port` — connection: numeric field.
   - `propresenter/batch-wait-time` — behavior: numeric field.
   - `propresenter/batch-max-count` — behavior: numeric field.
   - `propresenter/expire-time` — behavior: numeric field.
   - A **Save** button and a **Test** button (Test = reachability; wired in
     002-7 / 002-3's `ProPresenterClient::test()`).
4. **Prefill** every field from the injected `Config` on construction (and via a
   `reloadFrom(const Config&)` slot so 002-7 can refresh fields after a Save
   round-trip). Tokens are prefilled but masked (Decision 11). Record the
   prefilled text of each field as the **baseline** for dirty-tracking.
5. **Masked tokens + reveal toggle** (Decision 11): `bot-token`/`app-token`
   render password-style (dots); an eye toggle flips
   `QLineEdit::Password`↔`QLineEdit::Normal` to reveal/verify. No transformation
   on Save — the field text round-trips verbatim. Plaintext-in-`.ini` is
   unchanged from Spec 001; masking is shoulder-surf protection at the operator
   machine only, not at-rest encryption.
6. **Dirty-tracking** (Decision 5): compare each field's current text against its
   baseline. Expose per-section *connection* dirty accessors —
   `bool slackConnectionDirty() const` and `bool proPresConnectionDirty() const`
   — that return true only when at least one **connection** field in that section
   (per Decision 4) differs from baseline. **Behavior** fields
   (`slack/ignore-numbers`, `batch-wait-time`, `batch-max-count`, `expire-time`)
   never contribute to the reconnect gate. After a successful Save, 002-7 calls
   back into the tab (e.g. `commitBaseline(Section)` or `reloadFrom`) to reset the
   baseline so the section is no longer dirty.
7. **Accessors for the integrator**: expose typed getters for the current field
   values (e.g. `slackBotToken()`, `slackAppToken()`, `slackListenChannel()`,
   `slackIgnoreNumbers()`, `proPresHost()`, `proPresPort()`, `batchWaitTime()`,
   `batchMaxCount()`, `expireTime()`) so 002-7 can read the edited values and
   push them through `Config` setters. Numeric getters return the raw field text
   or a parsed int as convenient — validation/coercion policy lives in
   `Config::validate()` (002-1), not here.
8. **Signals for the integrator** (main.cpp owns wiring): declare
   `saveRequested(ConnectionsTab::Section)`,
   `reconnectRequested(ConnectionsTab::Section)`, and `testRequested()`
   (ProPresenter only). The Save/Reconnect/Test buttons emit these; the tab
   performs no side effects itself.
9. **Inline validation display** (Decision 6): add a slot
   `showValidation(const Config::ValidationResult&)` that renders results inline
   per field — **required-but-unset** as an error style/message next to the
   field, **present-but-malformed** as a warning (never silently coerce, per
   Decision 6). Clearing/refreshing on a subsequent call must remove stale
   markers. The two tiers must be visually distinguishable ("must fix to
   connect" vs. "this looks off").
10. **Config-path label + Reveal Config File** (Decision 13): show a read-only
    label with the resolved `Config::configPath()` so the operator can confirm
    which file is in effect, and add a **Reveal Config File** button that opens
    the file's location via
    `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` — mirroring the Reveal
    Log File button in `mainwindow.cpp`.
11. Register `src/ui/connectionstab.cpp` in the `qt_add_executable(ProPager ...)`
    source list in `CMakeLists.txt` (and update `target_include_directories` only
    if a new path is needed — `src/ui` is already on the include path). Confirm
    the project still builds with the widget compiled but not yet mounted.

## Acceptance Criteria

- [ ] `src/ui/connectionstab.{h,cpp}` exist; `ConnectionsTab : QWidget` takes a
  `const Config&` and exposes a `Section { Slack, ProPresenter }` enum.
- [ ] Both sections render every Decision-4 field: Slack (`bot-token`,
  `app-token`, `listen-channel`, `ignore-numbers`) and ProPresenter (`host`,
  `port`, `batch-wait-time`, `batch-max-count`, `expire-time`), each with its
  own Save and a Reconnect (Slack) / Test (ProPresenter) button.
- [ ] `bot-token` and `app-token` render masked (dots) and prefilled; an eye
  toggle reveals/hides the text; Save round-trips the field text verbatim (no
  transformation).
- [ ] Every field prefills from the injected `Config`; `reloadFrom(const Config&)`
  refreshes fields and resets the dirty baseline.
- [ ] `slackConnectionDirty()` / `proPresConnectionDirty()` return true iff a
  **connection** field in that section differs from baseline; editing a
  **behavior** field (`ignore-numbers`, `batch-wait-time`, `batch-max-count`,
  `expire-time`) leaves the connection-dirty flag **false**.
- [ ] After a Save-commit callback (`commitBaseline`/`reloadFrom`), the affected
  section is no longer connection-dirty.
- [ ] Typed value accessors expose the current (edited) contents of every field.
- [ ] `saveRequested(Section)`, `reconnectRequested(Section)`, and
  `testRequested()` are emitted by the corresponding buttons; the tab performs
  no `Config` write, controller reload, or client reconnect itself.
- [ ] `showValidation(const Config::ValidationResult&)` renders required-but-unset
  as an error and present-but-malformed as a warning, inline per field, visually
  distinct; a later call clears stale markers.
- [ ] A read-only label shows `Config::configPath()`, and a **Reveal Config File**
  button opens the file location via `QDesktopServices::openUrl`.
- [ ] `src/ui/connectionstab.cpp` is in `CMakeLists.txt`; the project builds with
  the widget compiled (it is not yet mounted in `MainWindow` — that is 002-5).
- [ ] No `Config` writes, no client calls, and no controller access occur inside
  `ConnectionsTab` — it is a pure form (matches Spec 001's main.cpp-as-integrator
  pattern; wiring is 002-7).

## Files Changed

| File | Action |
|---|---|
| src/ui/connectionstab.h | Create |
| src/ui/connectionstab.cpp | Create |
| CMakeLists.txt | Modify |

> The `CMakeLists.txt` edit only adds `src/ui/connectionstab.cpp` to the target so
> the widget compiles. Mounting the tab into `MainWindow`'s `QTabWidget`, and all
> Save/Reconnect/Test/validation wiring, are owned by 002-5 and 002-7
> respectively — this task ships a self-contained, compilable form.

## Verification

1. Build the project; confirm it compiles with `connectionstab.cpp` in the target
   and no other file yet references `ConnectionsTab`.
2. In a throwaway harness (or a temporary mount), construct a `ConnectionsTab`
   over a test `Config` and `show()` it. Confirm both sections render with all
   Decision-4 fields and their Save/Reconnect/Test buttons.
3. Confirm `bot-token`/`app-token` show as dots, prefilled from config, and the
   eye toggle reveals/hides the text.
4. Edit a **connection** field (e.g. `host`) and confirm
   `proPresConnectionDirty()` becomes true; edit only a **behavior** field (e.g.
   `expire-time`) and confirm the connection-dirty flag stays false.
5. Call `showValidation(...)` with a result carrying one required-missing key and
   one shape-warning; confirm the error and the warning render inline on the
   right fields, visually distinct, and that a follow-up call with an empty result
   clears them.
6. Confirm the config-path label matches `Config::configPath()` and that **Reveal
   Config File** opens the file's location in Finder.
7. Click Save/Reconnect/Test and confirm the corresponding signal fires (observed
   via a connected lambda in the harness) and that no `Config` write or client
   call happens inside the widget.
