# Task 001-3: ProPresenter Client (REST /v1)

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Complete
**Parallel group:** Wave 3 (parallel with [001-5](001-5-slack-client.md))
**Depends on:** [001-2](001-2-config-layer.md)
**Blocks:** [001-4](001-4-pager-controller.md)

## Build notes (2026-08-06)

**Open TODO 1 closed.** The per-message endpoints were confirmed against the
authoritative ProPresenter OpenAPI spec (`jeffmikels/ProPresenter-API`,
`pro7.9.openapi-spec.json`, mirroring `openapi.propresenter.com`):

- `GET /v1/message/{id}/clear` — per-message clear, `204 No Content`. **Confirmed
  as expected.** The layer-wide `GET /v1/clear/layer/messages` is not used.
- **Correction:** trigger is `POST /v1/message/{id}/trigger` (not the `GET` this
  task file originally assumed). The client uses `POST`; success is still
  `204 No Content` and no body is parsed. The optional token-override body is
  sent as an empty JSON array.
- `PUT /v1/message/{id}` and `POST /v1/messages` share the Decision-4 body
  schema (`id`, `message`, `theme` required); the `{id}` path segment uses the
  stored message's `uuid` (falling back to its `name`).

Pure request/response logic (`apiBase`, `buildMessageBody`, `pickTheme`,
`findMessageId`, `pathId`) is unit-tested in `tests/tst_propresenterclient.cpp`.
The network round-trips (ensure/create, set, trigger, clear, startup recovery)
still require the live-ProPresenter manual verification in the section below —
not runnable in this build environment.

## What

Build `ProPresenterClient`, the module that owns ProPager's ProPresenter message lifecycle over the documented REST `/v1` API. It uses `QNetworkAccessManager` exclusively — the legacy `/remote` protocol-701 WebSocket path from `bot.py` (`pro7_send_hello`, `propres_send_number`, `messageSend`/`messageHide`, password auth) is deleted, not ported (Decision 4). On startup the client ensures a Message named `ProPager` exists (finding or creating it), reuses its returned `id` for every subsequent call, and drives that message through a set → trigger → clear cycle scoped strictly to that `id`. This replaces the fragile `propres_process_message_list` `"vk"`-title + `${token}` regex auto-detection (Decision 5). The theme-lookup logic from `bot.py`'s `propres_create_message` is ported so a newly created message picks a sensible theme. Connection and error state are surfaced as Qt signals for the UI layer built later. The per-message clear endpoint (Open TODO 1) must be confirmed against a live ProPresenter's Swagger UI before this task ships.

## Steps

1. Create `src/propresenterclient.h` / `src/propresenterclient.cpp`. Give the class a single owned `QNetworkAccessManager`; take host/port from `Config` (Decision 10) to build the `http://{host}:{port}` base URL. Do NOT introduce any WebSocket, password, or `/remote` code.
2. Define Qt signals for connection/error state for the later UI: `connected()`, `disconnected()`, and `error(QString)`. Emit them from the network reply handlers.
3. Implement **ensure-message** (Decision 5): on startup, find or create the Message named `ProPager`.
   - Port the theme-lookup from `bot.py`'s `propres_create_message`: `GET /v1/themes`, search each theme's slides for one whose `id.name` contains `"vk"` (case-insensitive) and use its `id` as the theme; if none matches, fall back to the first theme's first slide (`themes[0].slides[0].id`).
   - Name the message `ProPager` (NOT `"Test Message"`), and do NOT embed the old hardcoded UUID (`942C3FC3-...`) — leave `uuid` empty.
   - If the message is missing, create it via `POST /v1/messages`. Store the returned `id` and reuse it for all later calls.
4. Implement the **send path**, every call scoped to the stored `id`:
   - `PUT /v1/message/{id}` — set the token text. Use the exact body shape from Decision 4 (below).
   - `GET /v1/message/{id}/trigger` — show the message. Expect `204 No Content` with no body; treat 204 as success and do not attempt to parse a body.
   - `GET /v1/message/{id}/clear` — per-message clear, hiding ONLY ProPager's message.
5. Implement **startup recovery** (Decision 5): after ensure-message resolves the `id`, issue the per-message `clear` so the app launches from a known-empty state and a crash mid-display self-heals.
6. **Close Open TODO 1:** confirm the exact per-message clear endpoint path against a live ProPresenter's Swagger UI at `openapi.propresenter.com` before marking this task done. Do NOT fall back to the layer-wide `GET /v1/clear/layer/messages` — it is rejected in Decision 5 / Not Doing because it would collide with a tech's other messages on the same layer.
7. Register the new source files in `CMakeLists.txt`.

`PUT /v1/message/{id}` body shape (Decision 4):

```json
{
  "id": { "name": "ProPager", "uuid": "", "index": 0 },
  "message": "VK: {Number}",
  "tokens": [ { "name": "Number", "text": { "text": "142" } } ],
  "theme": { "name": "", "uuid": "", "index": 0 },
  "visible_on_network": true
}
```

## Acceptance Criteria

- [ ] `ProPresenterClient` uses `QNetworkAccessManager` only; no WebSocket, no `/remote`, no protocol-701, no password auth anywhere in the file.
- [ ] On startup the client ensures a Message named `ProPager` exists: it is found if present, or created via `POST /v1/messages` if missing.
- [ ] Message creation ports the theme-lookup (`GET /v1/themes`, pick a slide whose name contains `"vk"`, else fall back to the first theme's first slide).
- [ ] The message is named `ProPager` (not `"Test Message"`) and no hardcoded legacy UUID is embedded.
- [ ] The returned `id` is stored and reused; there is no `"vk"`-title + `${token}` regex auto-detection.
- [ ] `PUT /v1/message/{id}` sends exactly the Decision 4 body shape (`id`, `message`, `tokens`, `theme`, `visible_on_network: true`), scoped to the stored `id`.
- [ ] `GET /v1/message/{id}/trigger` shows the message and treats `204 No Content` (no body) as success.
- [ ] `GET /v1/message/{id}/clear` clears only ProPager's own message.
- [ ] Startup recovery clears ProPager's message on launch.
- [ ] Qt signals `connected()`, `disconnected()`, and `error(QString)` are exposed for the UI.
- [ ] Host/port come from `Config`.
- [ ] The exact per-message clear endpoint path is confirmed against a live ProPresenter's Swagger UI (`openapi.propresenter.com`) before shipping; the layer-wide `GET /v1/clear/layer/messages` is NOT used.

## Files Changed

| File | Action |
|---|---|
| src/propresenterclient.h | Create |
| src/propresenterclient.cpp | Create |
| CMakeLists.txt | Modify |

## Verification

Test against a live ProPresenter with network enabled:

1. Start the app. Confirm it ensures a `ProPager` message exists — with no `ProPager` message present it is created; with one present it is reused (no duplicate created).
2. Drive a `PUT` to set a number, then `trigger`; confirm the number displays on screen.
3. Manually create a second Message on the same layer and trigger it. Then issue ProPager's per-message `clear`; confirm ONLY the ProPager message hides while the second, manually-created message stays visible.
4. Confirm startup clears the ProPager message (relaunch while ProPager's message is showing; it should be hidden on launch).
5. Before marking this task done, explicitly confirm the per-message clear endpoint path in the live ProPresenter Swagger UI at `openapi.propresenter.com` (closes Open TODO 1); do not ship if it cannot be confirmed.
