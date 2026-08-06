# Spec 001: ProPager — Native Qt6/C++ Rewrite

**Date:** 2026-08-06
**Status:** Draft
**Branch:** `master`

## Goals / Objective

Rewrite the inherited "Village Kids Pager" Python/PySide6 app as a native **Qt6/C++** macOS desktop app renamed **ProPager**. The goal is not new features — it is a foundation the maintainer can confidently own, sign, and ship for years. The current app carries technical debt concentrated in its *shell* (un-distributable Nuitka bundle, never signed/notarized, a self-hosted OAuth broker, janky token copy-paste) and its *concurrency model* (asyncio in a background thread bridged to Qt via signals plus a polling thread). A Qt6/C++ rewrite structurally eliminates the concurrency mess (one event loop) and makes signing/notarization clean (native code needs minimal hardened-runtime entitlements, unlike an embedded interpreter).

**Success criteria:**

1. A signed, notarized, stapled `.dmg` that runs on any Mac (arm64 + Intel) with **no runtime prerequisites** — no Python, no user-installed frameworks.
2. Behavioral parity with the current app's Slack→ProPresenter forwarding, including batching, queueing, and emoji feedback (see Decision 6).
3. Zero background threads, zero asyncio, zero cross-thread signal bridge — all I/O on the Qt event loop.
4. No self-hosted server component and no OAuth flow (tokens configured directly).

## Architecture Overview

Single-process, single-threaded (Qt event loop) desktop app. Two network clients and a display controller, coordinated by a queue/state machine, surfaced through a tray icon + status window.

```
                         ┌───────────────────────────────────────────┐
                         │                 ProPager                    │
                         │            (Qt6 event loop only)            │
   Slack (cloud)         │                                             │
   Socket Mode  ◀── WSS ─┤  SlackClient                                │
   Web API      ◀── HTTPS┤   • QWebSocket (Socket Mode envelopes)      │
                         │   • QNetworkAccessManager (reactions, convs)│
                         │        │                ▲                   │
                         │        │ number         │ emoji feedback    │
                         │        ▼                │ (⌛ 📞 👍)         │
                         │  PagerController  ──────────────────────────┤
                         │   • QQueue<Batch>                           │
                         │   • batch-window QTimer (debounce)          │
                         │   • expire QTimer (display duration)        │
                         │   • state: Idle | Showing(until T)          │
                         │        │                                    │
                         │        ▼                                    │
   ProPresenter (LAN)    │  ProPresenterClient                         │
   REST /v1     ◀── HTTP─┤   • QNetworkAccessManager only (no WS)      │
                         │   • PUT set token → trigger → clear (per-id)│
                         │        │                                    │
                         │        ▼                                    │
                         │  UI: QSystemTrayIcon + QMainWindow status   │
                         │  Config: QSettings (IniFormat)              │
                         └───────────────────────────────────────────┘
```

**Ownership model:** ProPager owns the *lifecycle* of its own ProPresenter message. It never relies on ProPresenter feedback (Pro7 sends none). It sets, triggers, and clears **its own message by id** on its own schedule, coexisting with any other messages a tech uses on the same layer.

## Decisions

### 1. Language & framework: Qt6 / C++

Native C++ on Qt6. The current UI is *already* Qt (via PySide6), so the UI ports near 1:1; the rewrite's value is in the non-UI layers. Rejected alternatives: staying Python (fails the "confidently own the toolchain" goal, and Nuitka fought the maintainer); embedding JS — see Decision 9.

### 2. Scope: single-tenant MVP

Village Church only, one Slack workspace, macOS only. Multi-tenancy is deliberately deferred (see Not Doing). This decision is what allows deleting the entire server/OAuth layer (Decision 3). The name "ProPager" is intentionally product-neutral so the multi-tenant door stays open later without a rename.

### 3. Delete the server component and OAuth flow

`server.py` (the aiohttp OAuth broker) and the entire OAuth token-exchange flow are **removed, not ported**. In a single-workspace deployment there is no need to broker per-workspace tokens without embedding a secret. The Slack **bot token** and **app-level token** are configured directly (Decision 8). This eliminates two of the four original debt items with no rewrite of app logic.

### 4. ProPresenter transport: REST `/v1` only, no legacy WebSocket

All ProPresenter I/O uses the documented REST `/v1` API via `QNetworkAccessManager`. The legacy `/remote` WebSocket protocol (protocol 701, password auth, `messageSend`/`messageHide`) is **removed entirely**, along with the ProPresenter password from config. The current app was schizophrenic — it *sent* over the legacy WebSocket but *created* messages over REST; this unifies on REST.

Send path, all scoped to a single message `id`:

| Call | Purpose |
|---|---|
| `PUT /v1/message/{id}` | Set the message token text (the number, or combined `"142, 143 & 144"`) |
| `GET /v1/message/{id}/trigger` | Show the message (returns `204 No Content` — no confirmation body) |
| `GET /v1/message/{id}/clear` *(verify path — see TODO 1)* | Hide **only** ProPager's message (per-message, not layer-wide) |

`PUT` body shape (confirmed against the spec):

```json
{
  "id": { "name": "ProPager", "uuid": "", "index": 0 },
  "message": "VK: {Number}",
  "tokens": [ { "name": "Number", "text": { "text": "142" } } ],
  "theme": { "name": "", "uuid": "", "index": 0 },
  "visible_on_network": true
}
```

### 5. Own the message lifecycle (Pro7-only, per-message clear)

Pro7 is the only supported ProPresenter version; Pro6 event handling is removed. Because Pro7 emits no feedback, ProPager does not guess — it **controls** the lifecycle:

- **Own a dedicated message.** At startup, ensure a message named `ProPager` exists (create via REST if missing); reuse its `id` thereafter. This replaces the fragile `propres_process_message_list` `"vk"`-title + `${token}` regex auto-detection, which is **removed**.
- **Display duration is a business rule, not a guess.** After `trigger`, start a `QTimer(expire-time)`; on fire, ProPager sends the per-message `clear` and advances the queue.
- **Per-message clear** scopes hiding to ProPager's `id` only, so a tech can run other messages on the same layer without collision. (This supersedes an earlier layer-wide `clear/layer/messages` approach.)
- **Startup recovery:** clear ProPager's own message on launch for a known-empty state; a crash mid-display self-heals.

The consequence: the `available` / `SetUnsetEvent` / `DoubleEvent` / nonce state machine from the current code is **removed**. State reduces to `Idle | Showing(until T)`.

### 6. Faithful behavioral parity: batching, queue, feedback

The interaction design *is* the product and is ported faithfully:

| Behavior | Detail |
|---|---|
| **Batching** | Numbers arriving within `batch-wait-time` are combined onto one message, formatted `"142, 143 & 144"`, up to `batch-max-count`. Overflow queues. |
| **Queue** | FIFO `QQueue` of batches; overflow waits for the slot to free (`expire-time`). |
| **Emoji feedback** | Reactions on the Slack message: ⌛ queued → 📞 on-screen → 👍 cleared. Now driven by ProPager's **own** state transitions (more reliable than the old guessed/absent Pro7 events). |
| **Commands** | `repeat` (re-send last number), `cancel` (clear current), `ignore-numbers` (❌ react, don't forward). |
| **Number match** | 4-digit number extracted from message text; messages starting with `!` ignored. |

Implemented with a `QQueue`, a debounce `QTimer` for the batch window, and the expiry `QTimer` — no threads.

### 7. Concurrency: one Qt event loop

`QWebSocket` (Slack Socket Mode), `QNetworkAccessManager` (Slack Web API + ProPresenter REST), and `QTimer` (batch window, display expiry) all run on the single Qt event loop. This deletes, wholesale: the `threading.Thread` running `asyncio.run`, the `Signal`-based cross-thread bridge, and the `overview.py` polling thread that reached into `client.number_queue._queue`. Status updates become direct signal/slot connections from the network clients to the UI.

### 8. Slack client: hand-rolled Socket Mode

There is no C++ Slack SDK, and embedding the JS SDK is rejected (Decision 9), so Socket Mode is implemented directly — a bounded ~300–500 lines, fully owned:

1. `POST https://slack.com/api/apps.connections.open` (app-level token) → `wss://` URL
2. `QWebSocket` connects; Slack pushes JSON **envelopes**
3. For each envelope: reply `{ "envelope_id": "..." }` to ack, then read `payload.event`
4. Filter `message` events on `listen-channel`; drive the PagerController
5. Feedback/queries via `QNetworkAccessManager`: `reactions.add`, `users.conversations`
6. Reconnect with backoff on socket close (the library behavior we're replacing)

### 9. Rejected: QJSEngine + bolt-js

Embedding Slack's JS SDK via `QJSEngine` is a category error. `QJSEngine` is a bare ECMAScript engine (no `require`, no npm, no Node built-ins, no event loop, no `net`/`tls`/`ws`); `bolt-js` is a Node.js app with a deep native dependency tree. The only way to run it is to bundle a full Node runtime as a sidecar — which reintroduces exactly the embedded-runtime packaging/signing debt the rewrite exists to eliminate. Hand-rolling (Decision 8) is both cleaner and more owned.

### 10. Config: QSettings (IniFormat)

Configuration via `QSettings` in `IniFormat`, resolved through `QStandardPaths` (see TODO 2). Keys:

| Section / key | Purpose |
|---|---|
| `slack/bot-token`, `slack/app-token` | Configured directly (no OAuth) |
| `slack/listen-channel` | Channel ID to listen on |
| `slack/ignore-numbers` | Numbers to ❌ rather than forward |
| `propresenter/host`, `propresenter/port` | REST endpoint (no password) |
| `propresenter/batch-wait-time`, `batch-max-count`, `expire-time` | Batching/display tuning |

No config is written to `~/Documents` (the current app's behavior is removed). In-app channel picker / editable channel ID stays post-MVP.

### 11. Identity & branding: ProPager

| Attribute | Value |
|---|---|
| App / display name, window title | `ProPager` |
| Bundle identifier | `com.isaacwiebe.propager` |
| `QSettings` org / app | (org) `com.isaacwiebe` / (app) `ProPager` |
| Log location | Under the app-support/`ProPager` dir (not `~/Documents`) |
| Icon assets | Rename from `data/village-kids-pager.iconset` |

Note: "ProPager" leans phonetically on "ProPresenter." Acceptable for an internal tool; revisit only if it becomes a distributed product.

### 12. Build, sign, ship

- **Qt source:** official Qt online installer (LGPL), which ships **universal2** (arm64 + x86_64) binaries. Homebrew Qt is single-arch and not used for release builds.
- **Build system:** CMake with `qt_generate_deploy_app_script` (the modern `macdeployqt`) to bundle Qt frameworks/plugins and fix rpaths into the `.app`.
- **Signing:** inside-out `codesign` (nested frameworks/plugins first, then the app), `--options runtime` (hardened runtime), `--timestamp`, minimal entitlements. Native C++ needs no `allow-unsigned-executable-memory` / `disable-library-validation` (unlike an embedded interpreter).
- **Notarization:** `xcrun notarytool submit --wait` then `xcrun stapler staple`, on both the `.app` and the final `.dmg`.
- **Delivery:** signed + notarized + stapled `.dmg`.
- **Where:** local Mac via a single `build.sh` (Developer ID cert already held; org-level, `com.isaacwiebe`). CI is post-MVP.

## Implementation Sequence

1. **Project skeleton** — CMake project, Qt6 dependency, `main.cpp` with `QApplication` and tray icon; app name/bundle id/`QSettings` identity set. Runs and shows a tray icon.
2. **Config layer** — `QSettings` read/write; resolve and document the actual on-disk path (closes TODO 2). Load all keys from Decision 10.
3. **ProPresenterClient** — REST `/v1` calls: ensure-message-exists (create if missing), `PUT` token, `trigger`, per-message `clear` (verify path, closes TODO 1). Manual-test against a live ProPresenter.
4. **PagerController** — `QQueue` + batch-window `QTimer` + expiry `QTimer`; `Idle | Showing` state machine; startup clear. Unit-drive with fake numbers (no Slack yet).
5. **SlackClient** — Socket Mode connect/ack/dispatch + Web API (`reactions.add`, `users.conversations`); reconnect backoff. Wire `message` events into the PagerController and state transitions back into emoji feedback.
6. **UI** — port `MainWindow` status window + tray menu (connection statuses, active/queued numbers) as direct signal/slot updates from the clients. No polling thread.
7. **Commands & edge cases** — `repeat`, `cancel`, `ignore-numbers`, `!`-prefix ignore, 4-digit extraction.
8. **Build pipeline** — CMake deploy script; `build.sh` doing build → deploy → codesign → notarytool → staple → `.dmg`. Produce a notarized `.dmg` that launches clean on a second Mac.

## File Plan

This is a full rewrite in a new language; the existing Python files are removed once parity is reached. Exact C++ file granularity is at implementation discretion — the plan below is the intended module shape.

### New Files

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Qt6 CMake project, deploy script, bundle metadata (name, id, icon) |
| `src/main.cpp` | `QApplication`, tray icon, wiring |
| `src/config.{h,cpp}` | `QSettings` (IniFormat) load/save |
| `src/slackclient.{h,cpp}` | Socket Mode (`QWebSocket`) + Web API (`QNetworkAccessManager`) |
| `src/propresenterclient.{h,cpp}` | REST `/v1` set/trigger/clear, ensure-message |
| `src/pagercontroller.{h,cpp}` | Queue, batching, expiry timers, state machine |
| `src/ui/mainwindow.{h,cpp}` | Status window (ports `ui/overview.py` + `ui/mainwindow.py`) |
| `src/ui/traymenu.{h,cpp}` | Tray icon + menu (ports `ui/widget.py`) |
| `build.sh` | Local build → deploy → sign → notarize → staple → `.dmg` |
| `entitlements.plist` | Minimal hardened-runtime entitlements |
| `data/propager.icns` | Renamed/rebuilt app icon |

### Removed Files (current Python app)

| File | Reason |
|---|---|
| `bot.py` | Replaced by `slackclient` + `propresenterclient` + `pagercontroller` (C++) |
| `main.py`, `ui/*.py` | Replaced by C++ UI |
| `server.py`, `server-config.example.toml` | OAuth broker deleted (Decision 3) |
| `config_example.py`, `config.example.toml` | Replaced by `QSettings` (Decision 10) |
| `nuitka-build.zsh`, `requirements.txt`, `*.pyproject` | Replaced by CMake + `build.sh` (Decision 12) |

## Not Doing (and Why)

- **Multi-tenancy / distributing to other churches** — deferred. Single-workspace is what lets us delete the server/OAuth. The name is kept product-neutral to revisit later.
- **Pro6 support** — removed. Pro7-only collapses the hardest part of the logic to a single timer. Can be re-added if a venue ever needs it.
- **The self-hosted OAuth server** — deleted. Tokens configured directly.
- **Layer-wide message clearing** — rejected in favor of per-message clear (Decision 5), so ProPager coexists with other Messages usage.
- **Windows / cross-platform** — not built now. Qt makes it near-free later; MVP is macOS-only.
- **In-app channel picker / editable channel ID** (README wishlist) — post-MVP; `QSettings` edit suffices.
- **Auto-update (e.g. Sparkle)** — post-MVP; manual `.dmg` redistribution for now.
- **Reacting to manual/external clears via `/v1/status/updates`** — post-MVP; owning the lifecycle makes it unnecessary for parity.
- **CI signing (GitHub Actions)** — post-MVP; local `build.sh` ships the MVP.
- **Embedding bolt-js via QJSEngine or a Node sidecar** — rejected (Decision 9).

## Open TODOs (verify at build time)

1. **Per-message clear path** — confirm the exact endpoint (expected `GET /v1/message/{id}/clear`) in the live ProPresenter Swagger UI (`openapi.propresenter.com`). The auto-summarized spec only surfaced the layer-wide `GET /v1/clear/layer/messages`; do not ship until the per-message path is confirmed against a running ProPresenter.
2. **Config path** — confirm the actual on-disk location `QSettings`/`QStandardPaths` resolves to for IniFormat/UserScope on macOS (expected under `~/Library/Application Support/ProPager/`), and point logs there too.

## Related

- Original app: `bot.py`, `server.py`, `ui/*.py` (Angelo Manca / AMANCA SOFTWARE), inherited and now maintained by Isaac Wiebe.
- ProPresenter REST API: `https://openapi.propresenter.com` (Message endpoints).
- Slack Socket Mode: `apps.connections.open`, Events API `message` events, Web API `reactions.add` / `users.conversations`.
- Grilling session (2026-08-06) that resolved the design decisions captured here.
