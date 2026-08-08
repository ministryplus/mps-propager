# Spec 002: Connections Tab — In-App Config, Validation & Reconnect

**Date:** 2026-08-07
**Status:** Complete
**Branch:** `master`

## Goals / Objective

Give the operator a single in-app surface to **see, edit, validate, and re-apply** connection configuration without hand-editing the `.ini` or restarting the app. This folds two open issues into one coherent feature:

- **#1** (manual reconnect for Slack + ProPresenter, no app restart)
- **#5** (config management + validation inside the app; surface missing/invalid config)

The unifying insight from the grilling session: both issues are fundamentally **per-connection** concerns, so they share one home and one code path. A new **Connections** tab presents Slack and ProPresenter side by side, each with its settings, a validation state, and Save/Reconnect/Test controls.

**Success criteria:**

1. First run with empty config shows an actionable "set your tokens" message (status-window banner + tray warning), **not** the silent `apps.connections.open` backoff loop that #5 complains about.
2. An operator can edit every connection setting in-app, hit **Save**, and have it persist to the `.ini` and take effect — reconnecting only the client whose connection changed.
3. Missing-required and present-but-malformed config are surfaced **outside the Log tab** (banner + tray), distinguishing "you must fix this to connect" from "this looks off."
4. Reconnect is reachable from both the tab and the tray, and re-establishes a dropped connection with no restart.

## Architecture Overview

The Connections tab is a form over `Config`. Save is a single action that fans out along a **dirty-tracked** path: always persist + reload, but reconnect only when a *connection* field changed. Behavior settings live on the same tab but reload without touching a socket.

```
   Connections tab (per-connection: Slack | ProPresenter)
        │  edit fields ─────────────────────────────┐
        │                                            │
        ▼  Save (per section)                        │
   ┌──────────────────────────────────────────┐     │
   │ 1. validate()  ── tiered ───────┐         │     │
   │      required-unset ────────────┼──▶ block reconnect
   │      present-but-malformed ─────┼──▶ warn, proceed
   │ 2. Config.setValue + sync  (write .ini)   │     │
   │ 3. reload into memory / controller        │     │
   │ 4. IF a connection field is dirty:        │     │
   │      reconnect that client only           │     │
   └──────────────────────────────────────────┘     │
        │                                            │
        ├── connection fields dirty ──▶ SlackClient.reconnectNow()  /  ProPresenterClient.reconnect()
        └── behavior fields dirty ────▶ PagerController setters (no socket churn)

   Cross-cutting (tab cannot host these):
        • Status-window banner  ── validation / connection warnings
        • Tray:  Reconnect (both clients, saved config)  +  warning state
```

**Why reconnect is gated on dirtiness:** reconnecting ProPresenter re-runs `ensureMessage()`, which issues the startup `clear` on ProPager's own message (Spec 001 Decision 5). A blind reconnect on every Save would **wipe a number currently on screen** whenever the operator tweaks a harmless timing value. Dirty-tracking confines that risk to actual connection changes.

## Decisions

### 1. A per-connection "Connections" tab (Overview / Connections / Log)

`MainWindow` gains a third tab, **Connections**, joining the existing Overview and Log tabs. It is organized into two sections — **Slack** and **ProPresenter** — each a small form plus a `Save` and a `Reconnect`/`Test` control. Overview keeps its live status/active/queue role; Log is unchanged.

This is *not* framed as a generic "Settings" tab. #4 (a manual Clear button) was explicitly considered and dropped — it is a pager **runtime action**, not a connection concern, and belongs on Overview + tray in its own issue.

### 2. Config becomes app-owned; guidance moves out of the `.ini`

Editing writes back via `QSettings::setValue` + `sync()`. **`QSettings` `IniFormat` does not preserve comments** — the first in-app Save collapses the currently commented template (`templateContents()`) to a bare `key=value` file. This is accepted: the `.ini` is now **app-owned**, and the guidance that lived in its comments (`; starts xoxb-`, etc.) moves into **field labels/tooltips on the Connections tab** and into the setup docs (#3).

`stripInlineComment()` and the first-run commented template are **kept** — they still serve reading a legacy or hand-edited file on load. Rejected: a comment-preserving custom serializer / surgical line-editor (more code, fragile, low value once the app is the primary editor).

### 3. Save = write → reload → reconnect (one action)

A section's **Save** persists that section's fields to the `.ini`, re-reads them into memory/controller, and — subject to Decision 5 — reconnects the client. The **Reconnect** button is the *same path with no field changes*: a force-reconnect from saved config. #1 and #5 therefore share one implementation, not two.

### 4. Field taxonomy: connection vs. behavior

Every editable key is classified. Only **connection** fields trigger a reconnect; **behavior** fields reload into `PagerController` with no socket churn.

| Section | Key | Class | On Save |
|---|---|---|---|
| Slack | `slack/bot-token` | connection (secret) | reconnect Slack |
| Slack | `slack/app-token` | connection (secret) | reconnect Slack |
| Slack | `slack/listen-channel` | connection | reconnect Slack |
| Slack | `slack/ignore-numbers` | behavior | reload controller |
| ProPresenter | `propresenter/host` | connection | reconnect PP |
| ProPresenter | `propresenter/port` | connection | reconnect PP |
| ProPresenter | `propresenter/batch-wait-time` | behavior | reload controller |
| ProPresenter | `propresenter/batch-max-count` | behavior | reload controller |
| ProPresenter | `propresenter/expire-time` | behavior | reload controller |

The behavior keys are filed under their existing `.ini` sections (matching Spec 001 Decision 10), even though they are conceptually pager behavior — they were crammed onto the Connections tab deliberately to keep a single settings surface.

### 5. Dirty-tracking gates the reconnect

Save **always** writes and reloads all of a section's values. It reconnects the client **only if at least one connection field (per Decision 4) actually changed**. Behavior-only edits reload silently. This is what prevents an `expire-time` edit from clearing a live on-screen number via a ProPresenter reconnect.

### 6. Tiered validation

Validation runs on load and on Save, producing a structured result the UI renders inline and in the cross-cutting surfaces (Decision 8). Two tiers:

| Tier | Keys | On failure |
|---|---|---|
| **Required-but-unset** | `slack/bot-token`, `slack/app-token`, `slack/listen-channel` | **Write** the `.ini` (don't lose typing) but **do not reconnect**; show inline error + banner + tray warning ("fill in X to connect"). |
| **Present-but-malformed** | shape checks below | **Warn** inline (never silently coerce) but **proceed** to save + reconnect. |

Shape checks (warn-only):

- `slack/bot-token` starts `xoxb-`
- `slack/app-token` starts `xapp-`
- `slack/listen-channel` looks like a Slack ID (`C…`)
- `propresenter/port` in `1–65535`
- `propresenter/batch-wait-time` / `batch-max-count` / `expire-time` positive integers

`propresenter/host` and `port` are **optional-with-default** (Spec 001 defaults `127.0.0.1` / `55184`), so their absence is fine; only a present-but-invalid value warns — replacing the current `intValueOr()` **silent coercion** with a visible warning.

### 7. Startup gating (kills the first-run backoff loop)

On launch the app runs the same `validate()`. If the **required-but-unset** Slack keys are missing, it **does not start the Slack connection** — no backoff loop, no cryptic `apps.connections.open returned no URL:` spam in the Log. Instead it surfaces the banner + tray warning immediately. This resolves #5's open question ("refuse to start the Slack connection … rather than looping on backoff forever") and its first-run acceptance criterion. The rule is identical to the Save gate (Decision 6), so there is one code path.

### 8. Cross-cutting surfaces: status-window banner + tray warning

The Connections tab shows inline field errors, but #5 requires config problems to be visible **without opening the tab**. Two surfaces the tab structurally cannot provide:

- **Status-window banner** (`MainWindow`): a prominent, dismissible-on-fix warning stating what's missing/malformed and how to fix it.
- **Tray warning state** (`TrayMenu`): a warning indicator (icon/tooltip) whenever config is invalid or a client is down.

### 9. Reconnect paths (the #1 core)

Both clients gain a re-runnable connect entry point, wired to Save/Reconnect and the tray:

- **`ProPresenterClient`** today runs `ensureMessage()` once at startup with no retry. Add a `reconnect()` that re-runs the connect/ensure path from current `Config`. It re-reads `m_config` live (the client holds `const Config&`), so no value snapshot is needed.
- **`SlackClient`** auto-reconnects with backoff; add `reconnectNow()` that **cancels the pending backoff timer and opens immediately** (resets backoff). Also reads `m_config` live.
- **`PagerController`** is the exception: `main.cpp` snapshots `batch-wait-time` / `batch-max-count` / `expire-time` into **`const int` members** at construction. To reload behavior settings live, drop the `const` and add setters (`setBatchWaitMs` / `setBatchMaxCount` / `setExpireMs`), applied on reload. This is the only genuinely new reload plumbing.

Reconnect must be safe to trigger while already connected (no duplicate sockets / leaked replies — #1 acceptance).

### 10. ProPresenter "Test" = reachability now, #2-ready

The ProPresenter section's **Test** button verifies the app can reach the REST API and adopt/create the ProPager message. It is **not** the deep #2 preflight (slide-label + message-shape checks) — #2 has an unresolved labelling-convention question (`vk` substring vs. explicit `propager`). The button is built as the seam #2 plugs into later, without blocking on #2. Slack's Test/Reconnect is unambiguous: auth works / socket opens.

### 11. Tokens: masked field + reveal toggle, prefilled

`bot-token` and `app-token` render as password-style fields (dots), prefilled from the `.ini`, with an eye toggle to reveal/verify. Save writes exactly what's in the field (simple round-trip). Plaintext-in-`.ini` is **unchanged** from Spec 001 — masking is shoulder-surfing protection at the operator machine, not at-rest encryption.

### 12. Tray: one coarse Reconnect + warning state

The tray has no per-connection UI, so it gets a **single `Reconnect`** that force-reconnects **both** clients from last-saved config (no editing in tray), plus the warning state from Decision 8. Per-connection granularity stays on the tab. This satisfies #1's "kick it without opening the window."

### 13. Discoverability: Reveal Config File + resolved path

Add a **Reveal Config File** affordance alongside the existing *Reveal Log File* button (`src/ui/mainwindow.cpp:69`), and surface the **resolved config path** (`Config::configPath()`) on the Connections tab so the operator can confirm which file is in effect. Satisfies #5's discoverability + effective-config criteria (the editable fields themselves *are* the effective config, tokens masked).

## Implementation Sequence

1. **Config write + reload + validate API** — add `setValue`-style setters per key, a `reload()`/re-sync, and `validate()` returning a structured result (required-missing list + shape-warning list, per Decision 6). Keep `stripInlineComment` and the first-run template for reads.
2. **PagerController live-reload** — drop `const` on the three timing members; add setters; apply on reload (Decision 9). Unit-drive: change a timing, confirm the next display honors it, no restart.
3. **Reconnect entry points** — `ProPresenterClient::reconnect()` (re-runnable ensure path) and `SlackClient::reconnectNow()` (cancel backoff, open now). Manual-test against live services; verify safe-while-connected.
4. **Connections tab UI** — `src/ui/connectionstab.{h,cpp}`: per-connection forms, masked tokens + reveal, Save/Reconnect/Test, inline validation display, dirty-tracking, config-path label + Reveal Config File.
5. **Cross-cutting surfaces** — status-window banner in `MainWindow`; tray warning state + tray `Reconnect` in `TrayMenu`.
6. **Wiring in `main.cpp`** — Connections-tab Save → `Config` write → conditional reconnect (dirty) + controller reload; startup validation gate (Decision 7); tray Reconnect → both clients; validation results → banner/tray.
7. **ProPresenter Test (reachability)** — hook up the reachability check; structure the result type so #2's deep preflight can extend it.

## File Plan

### New Files

| File | Purpose |
|---|---|
| `src/ui/connectionstab.{h,cpp}` | The Connections tab `QWidget`: per-connection forms, masked tokens, Save/Reconnect/Test, inline validation, dirty-tracking, config-path + Reveal Config |

### Modified Files

| File | Change |
|---|---|
| `src/config.{h,cpp}` | Add per-key setters + `reload()` + `validate()` (tiered result); keep `stripInlineComment` and first-run template for reads (Decisions 2, 6) |
| `src/pagercontroller.{h,cpp}` | Drop `const` on `m_batchWaitMs`/`m_batchMaxCount`/`m_expireMs`; add setters for live behavior reload (Decision 9) |
| `src/propresenterclient.{h,cpp}` | Add re-runnable `reconnect()` and a reachability `test()` (Decisions 9, 10) |
| `src/slackclient.{h,cpp}` | Add `reconnectNow()` (cancel backoff + open now) (Decision 9) |
| `src/ui/mainwindow.{h,cpp}` | Add Connections tab; add validation **banner**; add **Reveal Config File** alongside Reveal Log (Decisions 1, 8, 13) |
| `src/ui/traymenu.{h,cpp}` | Add **Reconnect** item (both clients) + **warning state** (Decisions 8, 12) |
| `src/main.cpp` | Wire Save → write + conditional reconnect + controller reload; startup validation gate; tray reconnect; validation → banner/tray (Decisions 3, 5, 6, 7) |

## Not Doing (and Why)

- **#4 — manual Clear button** — dropped from this spec. It's a pager runtime action (wraps `PagerController::cancel()`), not a connection concern; belongs on Overview + tray in its own issue.
- **#2 — deep ProPresenter preflight** (slide-label + message-shape checks) — out of scope; the Test button does reachability only and is built as the seam #2 extends later. #2 still owns the unresolved `vk`-vs-`propager` labelling decision.
  - **Learned during 002 live testing (fold into #2 when spec'd):** per-page display now shows the number by triggering with a **token override** (`POST /v1/message/{id}/trigger` body `[{"name":"Number","text":{"text":"<n>"}}]`, shape confirmed against the OpenAPI trigger schema) and issues **no per-page PUT** — a per-page PUT re-rendered the message and flashed stale content (the previous number, or a token UUID) before the correct value appeared. Consequence: the override only renders if the receiving **ProPager message already carries the `{Number}` token** in its text. So #2's preflight should:
    - **Verify (read):** `GET /v1/messages` (already in `test()`) → confirm the adopted message's text contains `{Number}` and a token named `Number`; report via new structured `TestResult` fields (e.g. `templateOk`/`tokenOk`) — the struct is the seam.
    - **Correct (write):** normalize a wrong/hand-made message with `PUT /v1/message/{id}` using our known-good body. Two constraints: **Test must stay side-effect-free (Decision 10)** — so the PUT belongs on connect/adopt (a one-time normalize in `ensureMessage`) or behind an explicit "Fix message" button, **not** in Test; and **PUT replaces the whole body incl. theme**, so the normalize must reuse `pickTheme` to avoid stomping the operator's chosen theme.
  - **Layer-status polling** (`GET /v1/status/layers`) exists and reports whether the messages layer is showing (but not *which* message). Noted as a possible #2/UI input (e.g. detect an external clear), but **deferred** — it reintroduces polling, which Spec 001 Decision 7 deliberately avoids, so it needs its own decision when #2 is spec'd.
- **#3 — setup docs + Help menu** — separate issue. This spec shifts config guidance *toward* the UI and those docs (Decision 2) but does not write them.
- **In-app Slack channel picker/dropdown for `listen-channel`** — kept a text field. `SlackClient` already has `listChannels()`/`fetchChannelList` plumbing, so a dropdown is a cheap follow-up, but it isn't required to close #1/#5.
- **Comment-preserving `.ini` serializer / surgical line-edit** — rejected (Decision 2); the `.ini` is app-owned.
- **A separate "Behavior" tab** — behavior settings share the Connections tab instead (Decision 4).
- **OS keychain / encrypted token storage** — tokens stay plaintext in the `.ini` (unchanged from Spec 001); masking is display-only (Decision 11).

## Verification

1. **First run, empty config** → banner + tray warning "set your Slack tokens"; **no** `apps.connections.open` backoff spam in the Log tab.
2. **Enter valid Slack `bot-token` + `app-token` + `listen-channel`, Save** → Slack connects; status labels flip to Connected on both surfaces.
3. **Edit `expire-time` only, Save while a number is on screen** → the on-screen number **stays** (no reconnect, no clear); the new duration applies to the next display.
4. **Edit `host`/`port`, Save** → ProPresenter reconnects; labels update.
5. **Typo `bot-token` (missing `xoxb-`)** → inline shape warning, but Save still writes and attempts reconnect.
6. **Blank a required field, Save** → inline error + banner; **no** reconnect; typed values in other fields are preserved.
7. **Reveal toggle** shows/hides the token text.
8. **Tray Reconnect while disconnected** → both clients re-attempt immediately (Slack backoff reset).
9. **Reveal Config File** opens the `.ini`'s location; the config-path label matches `Config::configPath()`.
10. **Post-Save `.ini`** is bare `key=value` (template comments gone) — expected per Decision 2.

## Related

- Issues **#1** (reconnect), **#5** (config management + validation); adjacent **#2** (PP preflight) and **#3** (setup docs / Help menu).
- Spec 001 Decisions **5** (own the message lifecycle / startup clear — the reconnect footgun), **10** (config keys + defaults), **11** (config/log location).
- Relevant code: `src/config.{h,cpp}` (`load`, `writeTemplateIfMissing`, `templateContents`, `intValueOr`, getters), `src/pagercontroller.{h,cpp}` (const timing members), `src/propresenterclient.cpp` (`ensureMessage`), `src/slackclient.cpp` (`openConnection`/`scheduleReconnect`, `fetchChannelList`), `src/ui/mainwindow.cpp` (tabs, Reveal Log button), `src/ui/traymenu.cpp`, `src/main.cpp` (wiring).
- Grilling session (2026-08-07) that resolved the decisions captured here.
