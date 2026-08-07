# Task 001-2: Config Layer

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Pending
**Parallel group:** Wave 2 (solo — blocks the clients)
**Depends on:** [001-1](001-1-project-skeleton.md)
**Blocks:** [001-3](001-3-propresenter-client.md), [001-5](001-5-slack-client.md)

## What

Introduce a `Config` module (`src/config.{h,cpp}`) that wraps `QSettings` in `IniFormat`/`UserScope`, resolved through `QStandardPaths`, and exposes typed getters for every configuration key defined in Decision 10. Defaults match the current Python app's `config.example.toml` so behavior carries over unchanged. On first run (or when keys are missing) the app writes a template `.ini` with commented defaults for the user to fill in, mirroring the current `setup_config`. This task also closes Open TODO 2 by resolving, logging, and recording the actual on-disk `.ini` path (expected under `~/Library/Application Support/ProPager/`) and by pointing the application log file into that same app-support/`ProPager` directory — the current app's `~/Documents/Village Kids Pager` behavior is removed (Decision 10/11). Note there is no `propresenter/password` key (Decision 4 removed the legacy WebSocket + password) and no `[network]`/OAuth keys (Decision 3).

## Steps

1. Create `src/config.h` declaring a `Config` class that owns a `QSettings` instance constructed with `IniFormat` + `UserScope` and the app identity (org `com.isaacwiebe`, app `ProPager`) established in Task 001-1. Resolve the concrete path via `QStandardPaths` (`AppConfigLocation` / `AppDataLocation`) so it is queryable.
2. Declare typed getters for all Decision-10 keys with the defaults below:
   - `slack/bot-token` → `QString` (default `""`)
   - `slack/app-token` → `QString` (default `""`)
   - `slack/listen-channel` → `QString`, channel ID (default `""`)
   - `slack/ignore-numbers` → `QStringList` (default `{"5555", "7777"}`)
   - `propresenter/host` → `QString` (default `"127.0.0.1"`)
   - `propresenter/port` → `int` (default `55184`)
   - `propresenter/batch-wait-time` → `int` (default `10`)
   - `propresenter/batch-max-count` → `int` (default `3`)
   - `propresenter/expire-time` → `int` (default `45`)
3. Implement `Config` in `src/config.cpp`: `load()` reads the `.ini`; each getter returns `settings.value(key, default)` coerced to its type so unset keys fall back to the documented default.
4. Add a `configPath()` accessor returning `settings.fileName()` (the resolved `.ini` path) and confirm the exact filename `QSettings` produces for org `com.isaacwiebe` / app `ProPager`.
5. On startup, if the `.ini` does not exist (or required keys are missing), write out a template `.ini` containing every key with commented defaults so the user can fill it in — parallels the current `setup_config`.
6. Point the application log file into the same app-support/`ProPager` directory as the `.ini` (NOT `~/Documents`); ensure the directory is created if absent.
7. Log the resolved `.ini` path at startup so it is visible on first launch (closes TODO 2), and record the observed path in the "Resolved path (TODO 2)" note below.
8. Wire `src/main.cpp` to construct a `Config`, call `load()`, and log the resolved path. Register `src/config.cpp` in `CMakeLists.txt`.

## Acceptance Criteria

- [ ] `src/config.h` and `src/config.cpp` exist; `Config` wraps `QSettings` in `IniFormat` + `UserScope` resolved via `QStandardPaths`.
- [ ] Typed getters exist for all nine Decision-10 keys, each returning the documented default when the key is unset.
- [ ] `ignore-numbers` default is `{"5555", "7777"}` and is exposed as a `QStringList`.
- [ ] No `propresenter/password` getter/key exists (Decision 4) and no `[network]`/OAuth keys exist (Decision 3).
- [ ] On first run the `.ini` is created at the resolved path with commented default values for every key.
- [ ] The resolved `.ini` path is logged at startup.
- [ ] The application log file is written under the app-support/`ProPager` directory, not `~/Documents`.
- [ ] Nothing is written under `~/Documents`.
- [ ] Editing a value and restarting round-trips (the edited value is read back).
- [ ] `main.cpp` constructs `Config`, loads it, and logs the path; `CMakeLists.txt` builds `config.cpp`.
- [ ] "Resolved path (TODO 2)" note below is filled in from a real machine.

## Files Changed

| File | Action |
|---|---|
| src/config.h | Create |
| src/config.cpp | Create |
| src/main.cpp | Modify |
| CMakeLists.txt | Modify |

## Verification

1. Build the project (`build` per Task 001-1 conventions).
2. On first run, confirm the `.ini` is created at the resolved path and that the path is printed to the log at startup.
3. Edit a value in the `.ini`, restart the app, and confirm the edited value is read back (round-trips).
4. Confirm the documented defaults appear for keys left unset.
5. Confirm nothing is written under `~/Documents` (check that neither the `.ini` nor the log file lands there).
6. Confirm the log file is created under the app-support/`ProPager` directory.

**Resolved path (TODO 2):** _(fill in once observed on a real machine — expected `~/Library/Application Support/ProPager/ProPager.ini`; confirm exact filename QSettings produces for org `com.isaacwiebe` / app `ProPager`)_
