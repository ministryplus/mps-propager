# Task 002-5: MainWindow — Connections Tab, Validation Banner & Reveal Config

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Parallel group:** Wave 3 (solo — must hold the `ConnectionsTab` and edit `mainwindow.*`)
**Depends on:** [002-1](002-1-config-api.md), [002-4](002-4-connections-tab.md)
**Blocks:** [002-7](002-7-wiring-startup-gate.md)

## What

Extend `MainWindow` (the status window from Task 001-6) to host the new
**Connections** tab and to surface config problems *without* opening that tab.
Three changes, all in `src/ui/mainwindow.{h,cpp}` (Spec 002 Decisions 1, 8, 13):

1. **A third tab, "Connections"** (Decision 1), inserted between the existing
   Overview and Log tabs so the order is **Overview / Connections / Log**.
   `MainWindow` constructs and owns a `ConnectionsTab` (Task 002-4) and re-exposes
   its high-level signals so `main.cpp` (Task 002-7) can wire Save/Reconnect/Test
   without reaching into the window internals.
2. **A validation banner** (Decision 8): a prominent, dismissible-on-fix strip
   above the tab widget that states what config is missing/malformed and how to
   fix it — the cross-cutting surface #5 requires to be visible even when the
   Connections tab is not open. It distinguishes *required-but-unset* ("fill in X
   to connect") from *present-but-malformed* ("this looks off").
3. **Reveal Config File discoverability** (Decision 13): ensure the operator can
   reach the on-disk `.ini` from the window. The primary config-path label +
   *Reveal Config File* affordance live on the Connections tab (Task 002-4); this
   task does **not** duplicate them (see Step 5).

The tab body itself — per-connection forms, masked tokens, dirty-tracking,
inline validation, the config-path label — is Task 002-4. This task only mounts
that widget and adds the window-level banner.

## Steps

1. **Ctor gains `Config` access.** The `ConnectionsTab` is a form over `Config`
   (Task 002-4), so `MainWindow` needs a `Config` handle to build it. Change the
   ctor to `MainWindow(PagerController *pager, Config *config, const QString &logDir, QWidget *parent = nullptr)`
   (add `Config *config` after `pager`; keep `logDir` for the existing Reveal Log
   button). Forward-declare `class Config;` in the header and store `Config *m_config`.
   The updated construction call in `main.cpp` is edited in Task 002-7 — declare
   the new ctor shape here so 002-7 can rely on it.
2. **Build and insert the Connections tab.** Add a private `QWidget *buildConnectionsTab()`
   that constructs a `ConnectionsTab` (passing `m_config`), stores it in a member
   `ConnectionsTab *m_connectionsTab`, and returns it. In the ctor, register the
   tabs in the new order:
   ```
   tabs->addTab(buildOverviewTab(), "Overview");
   tabs->addTab(buildConnectionsTab(), "Connections");
   tabs->addTab(buildLogTab(), "Log");
   ```
   (Today the ctor does `addTab(buildOverviewTab(), "Overview")` then
   `addTab(buildLogTab(), "Log")` — insert Connections between them.)
3. **Re-expose the tab's signals.** So `main.cpp` owns all wiring (matching the
   Spec 001 pattern where `main.cpp` is the integrator), give `MainWindow` a
   public accessor `ConnectionsTab *connectionsTab() const { return m_connectionsTab; }`.
   Task 002-7 connects the tab's `saveRequested` / `reconnectRequested` /
   `testRequested` signals (defined in 002-4) through this accessor. Do **not**
   re-emit them as duplicate `MainWindow` signals — a single accessor is the
   lighter seam and keeps the signal definitions in one place (002-4).
4. **Validation banner.** Restructure the central widget so a banner sits above
   the `QTabWidget`: a `QWidget *m_banner` (e.g. a colored `QFrame` holding a
   `QLabel *m_bannerLabel`) laid out in a `QVBoxLayout` with the tabs below it.
   Add a public slot `void showValidation(const Config::ValidationResult &result)`
   (type from Task 002-1) that:
   - **Hides** the banner when `result` is fully valid (no required-missing, no
     shape-warnings) — this is the "dismissible-on-fix" behavior.
   - Shows an **error-styled** banner when there are required-but-unset keys,
     listing them with fix guidance ("Fill in your Slack Bot Token, App Token,
     and Listen Channel to connect."). Required-missing takes visual precedence.
   - Shows a softer **warning-styled** banner when there are only shape-warnings
     (e.g. "Bot token doesn't start with `xoxb-` — this looks off."). The banner
     is informational here; connection still proceeds (Decision 6 tiering).
   Style the two tiers distinctly (e.g. red vs. amber background) so the operator
   can tell "must fix" from "looks off" at a glance.
5. **Reveal Config File placement decision.** The Connections tab (Task 002-4)
   owns the resolved config-path label **and** the primary *Reveal Config File*
   button (Decision 13), because that is where the operator reasons about which
   file is in effect. To satisfy Decision 13's discoverability goal without
   duplicating that control, `MainWindow` adds a **Reveal Config File** button on
   the **Log tab**, beside the existing *Reveal Log File* button
   (`mainwindow.cpp:69`), reusing the same pattern:
   `QDesktopServices::openUrl(QUrl::fromLocalFile(m_logDir))` — the config `.ini`
   and `ProPager.log` share the app-support/`ProPager` directory (Spec 001
   Decision 11 / Task 001-2), so `m_logDir` already resolves to the config
   directory and no new path member is needed. This gives a window-level reveal
   affordance that is reachable even before the Connections tab is opened, while
   the tab keeps the path-labeled version. Document this split in a comment so
   002-4 and 002-5 do not both claim the same button.

## Acceptance Criteria

- [ ] `MainWindow`'s ctor is `MainWindow(PagerController *pager, Config *config, const QString &logDir, QWidget *parent = nullptr)`; `class Config;` is forward-declared and a `Config *m_config` member is stored.
- [ ] The `QTabWidget` shows exactly three tabs in the order **Overview / Connections / Log**; the Connections tab hosts a `ConnectionsTab` built via `buildConnectionsTab()` and held in `m_connectionsTab`.
- [ ] A public accessor `ConnectionsTab *connectionsTab() const` exposes the tab so `main.cpp` (Task 002-7) can wire its Save/Reconnect/Test signals; `MainWindow` does not re-declare those signals itself.
- [ ] A banner widget sits **above** the tab widget and is controlled by a public slot `void showValidation(const Config::ValidationResult &result)`.
- [ ] With a fully-valid config the banner is **hidden** (dismissible-on-fix).
- [ ] With a required-but-unset config the banner shows an **error-styled** message naming the missing key(s) and how to fix them; with only shape-warnings it shows a distinct **warning-styled** message and does not read as a hard blocker.
- [ ] A **Reveal Config File** button exists on the Log tab beside the existing *Reveal Log File* button and opens `m_logDir` via `QDesktopServices::openUrl(QUrl::fromLocalFile(m_logDir))`; the Connections tab (002-4) retains the path-labeled reveal, and neither duplicates the other (documented in a comment).
- [ ] The existing Overview/Log behavior (`setSlackConnected`, `setProPresConnected`, `refresh`, `appendLog`, `closeEvent` hide-to-tray) is unchanged.
- [ ] The project builds; `main.cpp`'s current 3-argument `MainWindow(...)` call is the only remaining compile break, to be fixed in Task 002-7 (note this in the verification run).

## Files Changed

| File | Action |
|---|---|
| src/ui/mainwindow.h | Modify |
| src/ui/mainwindow.cpp | Modify |

## Verification

1. Build the project. Expect one intended compile error at the `MainWindow(&pager, config.configDir())` call in `main.cpp` (now missing the `Config*` argument); Task 002-7 fixes it. Temporarily pass `&config` locally to confirm the window itself compiles and links, then revert that probe.
2. Launch the app and open the window: confirm **three** tabs appear in the order **Overview / Connections / Log**, and the Connections tab renders the Task 002-4 widget.
3. Drive `showValidation()` with a `ValidationResult` for an **empty** config → the banner appears above the tabs with an error-styled "fill in your Slack tokens" message.
4. Drive `showValidation()` with a **valid** config → the banner is hidden (dismissible-on-fix).
5. Drive `showValidation()` with a **present-but-malformed** config (e.g. bot token missing `xoxb-`) → a distinct warning-styled banner appears and does not read as a hard blocker.
6. On the Log tab, click **Reveal Config File** → the app-support/`ProPager` directory (holding both the `.ini` and `ProPager.log`) opens in Finder, matching `Config::configPath()`'s directory.
