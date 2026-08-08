# ProPager - a Slack message bot for ProPresenter

## Release build

Run `./build.sh` to produce a signed, notarized, stapled `ProPager.dmg`.

* **Qt MUST come from the official Qt online installer (LGPL).** Its macOS
  binaries are **universal2** (x86_64 + arm64). **Homebrew Qt is single-arch and
  MUST NOT be used for release builds** — a Homebrew-linked binary can't be
  universal2 and will fail the `lipo -archs` check. `build.sh` points CMake at
  the online-installer Qt (default `~/src/Qt/6.8.3/macos`, override via
  `QT_DIR=`).
* **Minimum macOS: 12 (Monterey).** Set by `CMAKE_OSX_DEPLOYMENT_TARGET` in
  `CMakeLists.txt` and bounded by Qt 6.8 LTS (its frameworks' `minos` is 12.0).
* Requires a `Developer ID Application` cert (com.isaacwiebe) in the keychain
  and a stored `notarytool` credential profile — see the header of `build.sh`.

## Setup

### ProPresenter

* **ProPresenter ≥ 7.9** — the first version with the REST API ProPager drives
  (released 2022-04-06). Earlier 7.x only shipped the undocumented WebSocket
  remote protocol, which ProPager does **not** use.
* Enable the API in ProPresenter → **Preferences → Network**: turn Network on
  and note the **port** (ProPager talks to `http://<host>:<port>/v1`).

### Slack app configuration

ProPager connects over **Socket Mode** (no public URL / event request URL
needed) and requires **two tokens**: a **bot token** (`xoxb-…`) and an
**app-level token** (`xapp-…`).

1. **Create the app** — <https://api.slack.com/apps> → *Create New App* → *From
   scratch*. Name it **ProPager** and select your workspace.
2. **Enable Socket Mode** — *Settings → Socket Mode* → toggle on.
3. **App-level token** (`xapp-…`) — enabling Socket Mode prompts you to create
   one (else *Basic Information → App-Level Tokens → Generate*). Give it the
   **`connections:write`** scope. This is ProPager's **app-token**.
4. **Bot token scopes** — *OAuth & Permissions → Scopes → Bot Token Scopes*,
   add all of:

   | Scope | Why ProPager needs it |
   |---|---|
   | `channels:history` | Read messages in public channels it's added to |
   | `channels:join`    | Join public channels in the workspace |
   | `channels:read`    | List / look up public channels (channel picker) |
   | `chat:write`       | Send messages as @ProPager |
   | `groups:history`   | Read messages in private channels it's added to |
   | `reactions:read`   | View emoji reactions and their content |
   | `reactions:write`  | Add the ⌛ / 📞 / 👍 feedback reactions |

5. **Subscribe to message events** — *Event Subscriptions* → toggle on →
   *Subscribe to bot events* → add:

   | Event | Fires when | Scope Slack adds |
   |---|---|---|
   | `message.channels` | A message is posted to a public channel | `channels:history` |
   | `message.groups`   | A message is posted to a private channel | `groups:history` |

   (These are the same `*:history` scopes from step 4 — Slack adds them for you
   here.) Without these events, Socket Mode delivers no messages and ProPager
   sees nothing.
6. **Install to workspace** — *OAuth & Permissions → Install to Workspace*
   (re-install here whenever you change scopes). Copy the **Bot User OAuth
   Token** (`xoxb-…`) — this is ProPager's **bot-token**.
7. **Invite the bot** to the channel it should watch (`/invite @ProPager`).
8. **Enter both tokens and the channel** in ProPager's **Connections** tab:
   bot token `xoxb-…`, app-level token `xapp-…`.

## Acknowledgements

ProPager is a native Qt6/C++ rewrite of the original
[**propresenter-slack-forwarder**](https://github.com/IAmTomahawkx/propresenter-slack-forwarder)
by [IAmTomahawkx](https://github.com/IAmTomahawkx). That project — a Python/PyQt
app driving ProPresenter over its WebSocket remote protocol with `slack-bolt` —
is the conceptual origin of this one and the reference for its behavior
(number batching, the Slack command grammar, and the Slack ⇄ ProPresenter flow).
Full credit for the original idea and the legacy implementation goes to
IAmTomahawkx.

## License

Released under the [MIT License](LICENSE) — Copyright (c) 2026 Ministry Plus Solutions Inc.
