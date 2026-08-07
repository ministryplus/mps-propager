# Task 002-3: Reconnect Entry Points (ProPresenter + Slack)

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Done
**Parallel group:** Wave 1 (parallel with 002-1, 002-2 — disjoint files, both clients owned here)
**Depends on:** —
**Blocks:** [002-7](002-7-wiring-startup-gate.md)

## What

Give both connection clients a **re-runnable connect entry point** so #1's manual
reconnect (from the Connections tab and the tray) has something to call, and add
a ProPresenter **reachability Test** that is the seam #2's deep preflight extends
later. `ProPresenterClient` today runs `ensureMessage()` exactly once at startup
with no retry; `SlackClient` auto-reconnects with exponential backoff but offers
no way to skip the wait. This task adds:

- `ProPresenterClient::reconnect()` — re-runs the connect/ensure path
  (`ensureMessage()`) from current `Config`. The client holds `const Config&`
  (`m_config`) and reads it live, so no value snapshot is needed — reconnect just
  re-reads host/port. **Footgun (Decision 5):** `ensureMessage()` issues the
  startup `clear` on ProPager's own message, which is exactly why 002-7 gates PP
  reconnect on dirty *connection* fields. This task only provides the re-runnable
  method; it does **not** decide when to call it.
- `ProPresenterClient::test()` — a reachability check that verifies the app can
  reach the REST API and adopt/create the ProPager message. It returns a
  **structured result type** so #2's deep preflight (slide-label + message-shape
  checks) can extend it later without reshaping the call site. It is **not** the
  deep #2 preflight — reachability only (Decision 10).
- `SlackClient::reconnectNow()` — cancels the pending backoff timer, resets
  backoff, and opens the socket immediately (re-runs `openConnection()`), reading
  `m_config` live. This is the "kick it now" that skips the exponential backoff
  that `scheduleReconnect()` accumulates.

Both reconnect entry points must be **safe to trigger while already connected**:
no duplicate sockets, no leaked `QNetworkReply`s (#1 acceptance). Neither is wired
to any UI here — that is Task 002-7. This task leaves the app compiling and
behaving exactly as before because nothing calls the new methods yet.

## Steps

1. **`ProPresenterClient::reconnect()`** (`propresenterclient.h`/`.cpp`): declare a
   public slot `void reconnect()`. Implement it to re-run the connect/ensure path
   by calling `ensureMessage()`, which re-reads `m_config` (host/port via
   `apiBase()`) live and re-resolves `m_messageId` (find-or-`createMessage()`).
   Because `ensureMessage()` already performs a find → optional create → `clear`,
   `reconnect()` can be a thin re-entry that resets any stale state first.
2. **Safe-while-connected (ProPresenter):** before kicking off a new
   `ensureMessage()`, guard against overlapping in-flight REST work — abort/track
   the outstanding `QNetworkReply` from a previous ensure (or ignore its late
   finish) so a second `reconnect()` cannot leak a reply or double-resolve
   `m_messageId`. Re-resolving `m_messageId` on every reconnect is intentional
   (host may have changed); document that a stale id is discarded, not reused.
3. **`ProPresenterClient::test()`** (`propresenterclient.h`/`.cpp`): define a small
   structured result type (e.g. `struct TestResult { bool reachable; bool
   messageReady; QString detail; };`) declared in the header so callers and #2 can
   consume it. Implement `test()` to (a) confirm the REST API at
   `apiBase(host, port)` is reachable and (b) confirm the ProPager message can be
   adopted or created (the same find-or-create logic as `ensureMessage()`, but
   **without** driving set/trigger/clear). Report the outcome via the structured
   result — expose it as a signal (e.g. `void tested(const TestResult&)`) so the
   async REST call can report back without blocking. Leave room in the struct/
   result for #2's later slide-label + message-shape fields; do not add them now.
4. **`SlackClient::reconnectNow()`** (`slackclient.h`/`.cpp`): declare a public slot
   `void reconnectNow()`. Implement it to (a) stop the pending backoff via
   `m_reconnectTimer.stop()`, (b) reset backoff with `m_backoffMs = 0`, and (c)
   re-run `openConnection()` immediately (POST `apps.connections.open`, then open
   the socket). Reads `m_config` (bot/app tokens, listen channel) live.
5. **Safe-while-connected (Slack):** before reopening, if `m_socket` is already
   open/connecting, close it cleanly (`m_socket.close()` / abort the pending
   `apps.connections.open` reply) so `reconnectNow()` cannot leave two live
   `QWebSocket` connections or a leaked handshake reply. A `reconnectNow()` while
   healthy should tear down and re-establish exactly one socket.
6. Keep all existing behavior intact: `scheduleReconnect()`'s automatic backoff
   path is unchanged; `reconnectNow()` is purely an additional manual entry point
   that resets and short-circuits it. No source snapshots the config — both
   clients continue to read `m_config` live.

## Acceptance Criteria

- [ ] `ProPresenterClient::reconnect()` exists (public slot) and re-runs the
      connect/ensure path from current `Config` via `ensureMessage()`, re-reading
      host/port live (no snapshot).
- [ ] Calling `reconnect()` while already connected does **not** leak a
      `QNetworkReply` or create a duplicate resolution — a prior in-flight ensure
      is aborted/ignored and `m_messageId` is re-resolved cleanly.
- [ ] `ProPresenterClient::test()` exists and reports reachability + message
      adopt/create readiness via a **structured result type** declared in the
      header; it performs **no** set/trigger/clear and does **not** implement the
      deep #2 preflight.
- [ ] The `test()` result type has room for #2's later slide-label / message-shape
      checks (documented as the extension seam), without those checks present now.
- [ ] `SlackClient::reconnectNow()` exists (public slot): it stops
      `m_reconnectTimer`, resets `m_backoffMs` to 0, and re-runs `openConnection()`
      immediately, reading `m_config` live.
- [ ] Calling `reconnectNow()` while already connected tears down the existing
      `m_socket` (and any pending handshake reply) first — exactly one live
      `QWebSocket` afterward, no leaked reply.
- [ ] The existing automatic-backoff path (`scheduleReconnect()`) is unchanged;
      `emit connected()/disconnected()/error()` semantics are preserved.
- [ ] The app still compiles and behaves identically at runtime — none of the new
      methods are wired to UI yet (that is Task 002-7).

## Files Changed

| File | Action |
|---|---|
| src/propresenterclient.h | Modify |
| src/propresenterclient.cpp | Modify |
| src/slackclient.h | Modify |
| src/slackclient.cpp | Modify |

## Verification

Manual test against live services (no automated harness — these are network entry
points). Build per project conventions, then:

1. **PP reconnect while connected:** with ProPresenter running and the app
   connected, trigger `reconnect()`. Confirm `connected()` re-fires, the ProPager
   message is re-resolved, and there is exactly one outstanding request at a time
   (watch the log — no stacked/leaked replies, no double `m_messageId` churn).
2. **PP reconnect after a drop:** quit ProPresenter, confirm `disconnected()`/
   `error()`; relaunch ProPresenter and trigger `reconnect()` — the client
   re-establishes and re-adopts the message with no restart.
3. **PP test() reachable:** with ProPresenter up, call `test()` and confirm the
   structured result reports `reachable = true` / `messageReady = true` and that
   **no** number was set/triggered/cleared on screen (reachability only).
4. **PP test() unreachable:** point host/port at a dead endpoint, call `test()`,
   and confirm the result reports `reachable = false` with a useful `detail`
   string (surfaced later by the tab in 002-4/002-7).
5. **Slack reconnectNow resets backoff:** force the client into backoff (bad
   token or dropped socket so `scheduleReconnect()` has grown `m_backoffMs`), then
   call `reconnectNow()` — confirm the pending timer is cancelled and
   `openConnection()` runs immediately (no wait), and `m_backoffMs` is back to 0.
6. **Slack reconnectNow while connected:** with the socket healthy, call
   `reconnectNow()` — confirm the old socket is closed and exactly one new
   `QWebSocket` is established (no duplicate sockets, no leaked
   `apps.connections.open` reply).
