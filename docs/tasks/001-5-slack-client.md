# Task 001-5: Slack Client (Socket Mode + Web API)

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Complete
**Parallel group:** Wave 3 (parallel with [001-3](001-3-propresenter-client.md))
**Depends on:** [001-2](001-2-config-layer.md)
**Blocks:** [001-4](001-4-pager-controller.md)

## What

Build `SlackClient`, a hand-rolled Slack Socket Mode + Web API client with no SDK (Decision 8). Socket Mode runs over a `QWebSocket`; the Web API is called through `QNetworkAccessManager`. Everything lives on the single Qt event loop — no threads, no asyncio, no cross-thread bridge (Decision 7). Embedding bolt-js via `QJSEngine` or a Node sidecar is explicitly rejected (Decision 9) and must not be attempted. This client establishes the Socket Mode connection, acks and dispatches envelopes, delivers raw `message` events from the configured listen channel upward as a signal, and exposes Web API helpers for emoji feedback (`reactions.add`) and channel discovery (`users.conversations`). It does **not** parse commands or extract numbers — that is Task 7. It only delivers the raw message text, timestamp, and channel, plus the connection lifecycle. Auth uses the bot token and app-level token configured directly in Config (Decision 3); there is no OAuth flow and no `server.py` token fetch — `fetch_tokens` / the `[network]` config section is removed and must not be ported.

## Steps

1. Create `src/slackclient.{h,cpp}` declaring a `SlackClient` QObject that takes a reference/pointer to the `Config` (from Task 001-2) for the bot token, app-level token, and `slack/listen-channel`.
2. Hold a `QWebSocket` (Socket Mode) and a `QNetworkAccessManager` (Web API) as members; do not spawn any threads.
3. Implement the connect handshake: `POST https://slack.com/api/apps.connections.open` with the app-level token (Bearer auth), parse the JSON response for the `wss://` URL, and open the `QWebSocket` to it.
4. On WebSocket text messages, parse each JSON envelope: if it carries an `envelope_id`, immediately send back `{"envelope_id": "<id>"}` over the socket to ack, then read `payload.event`.
5. Filter to `message`-type events whose channel equals `slack/listen-channel`; for each, emit `messageReceived(QString text, QString ts, QString channel)` carrying the raw `text`, `ts`, and `channel`. Do not parse commands, the `!` prefix, or 4-digit numbers here (that is Task 7).
6. Implement `reactions.add` as a slot the feedback layer calls with an emoji name + channel + ts, issuing `POST https://slack.com/api/reactions.add` via `QNetworkAccessManager` with the bot token.
7. Implement `users.conversations` (port `fetch_channel_list` from `bot.py`): call `GET/POST https://slack.com/api/users.conversations` with `exclude_archived=true`, filter to entries that are channels, and return/emit the bot's list of `{name, id}` channels.
8. Implement reconnect with backoff on WebSocket `disconnected`/`error` (this is the slack-bolt library behavior being replaced): schedule a reconnect via `QTimer`, re-run `apps.connections.open` to obtain a fresh `wss://` URL, and re-open the socket.
9. Emit `connected()` when the Socket Mode socket opens and `disconnected()` when it closes, and log both, for UI/status consumption.
10. Register `src/slackclient.cpp` in `CMakeLists.txt` and ensure the `WebSockets` and `Network` Qt6 components are linked.

## Acceptance Criteria

- [ ] `src/slackclient.{h,cpp}` exist; `SlackClient` is a QObject with no threads (all I/O on the Qt event loop).
- [ ] No Slack SDK, no `QJSEngine`, no bolt-js, no Node sidecar is introduced (Decisions 8 & 9).
- [ ] Connect handshake POSTs to `apps.connections.open` with the app-level token and opens a `QWebSocket` to the returned `wss://` URL.
- [ ] Each incoming envelope with an `envelope_id` is acked with `{"envelope_id": "..."}` before its `payload.event` is processed.
- [ ] Only `message` events on `slack/listen-channel` are emitted; they surface via `messageReceived(QString text, QString ts, QString channel)` with the raw text/ts/channel and no command parsing.
- [ ] A `reactions.add` slot posts to the Web API with an emoji name + channel + ts using the bot token.
- [ ] `users.conversations` lists the bot's non-archived channels (ported `fetch_channel_list`).
- [ ] On socket close/error the client reconnects with backoff, re-running `apps.connections.open`.
- [ ] Auth uses bot + app tokens from Config only; no OAuth flow and no `fetch_tokens` / `[network]` behavior is present.
- [ ] `connected()` and `disconnected()` signals are emitted on socket lifecycle changes.

## Files Changed

| File | Action |
|---|---|
| src/slackclient.h | Create |
| src/slackclient.cpp | Create |
| CMakeLists.txt | Modify |

## Build notes (2026-08-06)

Implemented as a QObject over a single `QWebSocket` (Socket Mode) + one
`QNetworkAccessManager` (Web API), no threads (Decision 7); no SDK / QJSEngine /
bolt-js / Node sidecar (Decisions 8 & 9). Auth is bot + app tokens from
`Config` only — no OAuth, no `fetch_tokens`, no `[network]` section (Decision 3).

Pure envelope/response logic (`buildAck`, `extractMessageEvent`,
`wssUrlFromConnectionsOpen`, `channelsFromConversations`, `nextBackoffMs`) is
unit-tested in `tests/tst_slackclient.cpp`. Reconnect backoff is exponential
from 1s, capped at 30s, and resets on a healthy connect. The live round-trips
(real `apps.connections.open` handshake, `messageReceived` from a posted Slack
message, `reactions.add`, `users.conversations`, socket-drop reconnect) require
real Slack tokens and are covered by the manual verification below — not
runnable in this build environment.

## Verification

With valid bot + app tokens in config: the client calls `apps.connections.open`, opens the WSS socket, and logs `connected`. Posting a message in the listen channel emits `messageReceived` with the correct text/ts/channel. A `reactions.add` call adds an emoji to that message (verify it appears in Slack). Killing the socket (or a network blip) triggers a backoff reconnect that re-runs `apps.connections.open` and re-establishes the session. `users.conversations` returns the bot's channels.
