# TODOs

## Release build (Qt6/C++ rewrite)
Run `./build.sh` to produce a signed, notarized, stapled `ProPager.dmg`.

* **Qt MUST come from the official Qt online installer (LGPL).** Its macOS
  binaries are **universal2** (x86_64 + arm64). **Homebrew Qt is single-arch and
  MUST NOT be used for release builds** — a Homebrew-linked binary can't be
  universal2 and will fail the `lipo -archs` check. `build.sh` points CMake at
  the online-installer Qt (default `~/src/Qt/6.8.3/macos`, override via
  `QT_DIR=`).
* **Minimum macOS: 12 (Monterey).** Set by `CMAKE_OSX_DEPLOYMENT_TARGET` in
  `CMakeLists.txt` and bounded by Qt 6.8 LTS (its frameworks' `minos` is 12.0).
  Targeting macOS 11 would require Qt 6.5 LTS instead.
* Requires a `Developer ID Application` cert (com.isaacwiebe) in the keychain
  and a stored `notarytool` credential profile — see the header of `build.sh`.

## Build Environment notes (legacy Python app)
* Nuitka only supports Python 3.4 — 3.11
* Cross-compiling not supported, must build on arm64e or x86_64 directly

## Documentation
* Slack setup: 
    - Install application in Slack Workspace
    - add the Bot as an app
    - invite to Channel by messaging '@Number Service', channel must be public, find Channel ID on channel about page
- ProPresenter: First message in the list, must have a token of 'Message', name doesn't matter, 'allow Web notif...' doesn't matter


## Coding
- Better error handling for ProPresenter password missing
- ProPresenter fails silently, if the first message type doesn't have a token
- Change Channel ID with an Input field
- Can we pull a list of available channels?
- Add logging level to config file, duplicate console out to log file for Debug level

## Acknowledgements

ProPager is a native Qt6/C++ rewrite of the original
[**propresenter-slack-forwarder**](https://github.com/IAmTomahawkx/propresenter-slack-forwarder)
by [IAmTomahawkx](https://github.com/IAmTomahawkx). That project — a Python/PyQt
app driving ProPresenter over its WebSocket remote protocol with `slack-bolt` —
is the conceptual origin of this one and the reference for its behavior
(number batching, the Slack command grammar, and the Slack ⇄ ProPresenter flow).
Full credit for the original idea and the legacy implementation goes to
IAmTomahawkx.

The legacy Python sources (`bot.py`, `main.py`, `ui/*.py`, `server.py`, …) remain
in this repository as a reference during the rewrite and are removed once parity
is confirmed (see `docs/tasks/001-8-build-pipeline.md`).

## License

Released under the [MIT License](LICENSE) — Copyright (c) 2026 Ministry Plus Solutions Inc.

