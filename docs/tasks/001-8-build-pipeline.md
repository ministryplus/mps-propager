# Task 001-8: Build, Sign & Ship Pipeline

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Pending
**Parallel group:** Wave 6 (solo — needs the full app)
**Depends on:** [001-6](001-6-ui.md), [001-7](001-7-commands-edge-cases.md)
**Blocks:** —

## What

Stand up the local build-and-ship pipeline that turns the finished Qt6/C++ app into a signed, notarized, stapled `.dmg` that launches with **no runtime prerequisites** on any Mac (arm64 + Intel). This means sourcing Qt from the official online installer so the binaries are **universal2**, wiring `qt_generate_deploy_app_script(...)` and bundle metadata (name, identifier, icon) into CMake, producing the ProPager `.icns` icon, and authoring a single `build.sh` that runs the full configure → build → deploy → codesign → notarize → staple → `.dmg` sequence with a minimal hardened-runtime `entitlements.plist`. Only once end-to-end parity with the old app is confirmed do we delete the Python originals per the spec's Removed Files table, keeping the old app runnable as a reference until that point. CI signing (GitHub Actions) is explicitly post-MVP — the local `build.sh` ships the MVP.

## Steps

1. **Source universal2 Qt.** Install Qt6 via the official **Qt online installer** (LGPL), which ships **universal2** (arm64 + x86_64) binaries. Document — in this task and in `build.sh`/README notes — that Homebrew Qt is **single-arch** and must **NOT** be used for release builds; point CMake at the online-installer Qt for release configures.
2. **Create the app icon (Decision 11).** Produce `data/propager.icns`, rebuilt/renamed from the existing `data/village-kids-pager.iconset` (e.g. `iconutil -c icns` from the reworked iconset). This closes the icon note in Decision 11.
3. **Wire CMake bundle + deploy.** In `CMakeLists.txt`, add `MACOSX_BUNDLE` metadata — display name `ProPager`, bundle identifier `com.isaacwiebe.propager` — reference the app icon (`data/propager.icns`), and add `qt_generate_deploy_app_script(...)` (the modern `macdeployqt`) to bundle Qt frameworks/plugins and fix rpaths into the `.app`.
4. **Write `entitlements.plist`.** Minimal hardened-runtime entitlements only. Native C++ needs **NO** `allow-unsigned-executable-memory` and **NO** `disable-library-validation` — call out explicitly in a comment that those were only needed for the old embedded Python interpreter and are deliberately omitted.
5. **Author `build.sh`**, doing in order:
   1. `cmake` configure + build — **Release, universal2** (against online-installer Qt).
   2. Run the CMake-**generated deploy script** to bundle Qt frameworks/plugins and fix rpaths into the `.app`.
   3. **Inside-out `codesign`**: sign nested frameworks/plugins **FIRST**, then the `.app` last — each with `--options runtime` (hardened runtime), `--timestamp`, and the minimal `entitlements.plist`. Use the org-level Developer ID cert (`com.isaacwiebe`).
   4. `xcrun notarytool submit --wait`.
   5. `xcrun stapler staple` on the `.app`.
   6. Build the `.dmg`.
   7. `xcrun stapler staple` on the final `.dmg` (staple **both** the `.app` and the `.dmg`).
6. **Confirm parity first, then clean up.** Only **after** end-to-end parity with the old app is confirmed, delete the Python originals per the spec's Removed Files table: `bot.py`, `main.py`, `ui/*.py`, `server.py`, `server-config.example.toml`, `config_example.py`, `config.example.toml`, `nuitka-build.zsh`, `requirements.txt`, `*.pyproject`. Keep the old app runnable as a reference until this point.
7. **Verify on a second Mac** per the Verification section below.

## Acceptance Criteria

- [ ] Qt is sourced from the official Qt online installer (LGPL, universal2); Homebrew Qt is documented as forbidden for release builds.
- [ ] `data/propager.icns` exists, rebuilt/renamed from `data/village-kids-pager.iconset`.
- [ ] `CMakeLists.txt` sets `MACOSX_BUNDLE` metadata (display name `ProPager`, identifier `com.isaacwiebe.propager`), references the app icon, and calls `qt_generate_deploy_app_script(...)`.
- [ ] `entitlements.plist` contains only minimal hardened-runtime entitlements — no `allow-unsigned-executable-memory`, no `disable-library-validation` (with a comment explaining why).
- [ ] `build.sh` runs, in order: cmake configure+build (Release, universal2) → generated deploy script → inside-out codesign (nested first, `.app` last) with `--options runtime`, `--timestamp`, entitlements → `notarytool submit --wait` → `stapler staple` (`.app`) → build `.dmg` → `stapler staple` (`.dmg`), using the `com.isaacwiebe` Developer ID cert.
- [ ] `./build.sh` produces a signed, notarized, stapled `.dmg`.
- [ ] `spctl -a -vvv` and `codesign --verify --deep --strict` pass on the `.app`.
- [ ] `xcrun stapler validate` passes on **both** the `.app` and the `.dmg`.
- [ ] `lipo -archs` on the main binary shows `x86_64 arm64`.
- [ ] The `.dmg` launches clean on a SECOND Mac with no Python and no Qt installed — mounts, app opens to a working tray icon, no Gatekeeper warning, no runtime prerequisites.
- [ ] Python originals deleted per the Removed Files table, done LAST and only after parity is confirmed.

## Files Changed

| File | Action |
|---|---|
| build.sh | Create |
| entitlements.plist | Create |
| CMakeLists.txt | Modify |
| data/propager.icns | Create |
| bot.py, main.py, ui/*.py, server.py, config_example.py, config.example.toml, server-config.example.toml, nuitka-build.zsh, requirements.txt, *.pyproject | Delete |

## Verification

`./build.sh` produces a `.dmg`; `spctl -a -vvv` and `codesign --verify --deep --strict` pass on the `.app`; `xcrun stapler validate` passes on both `.app` and `.dmg`; `lipo -archs` on the binary shows `x86_64 arm64`; the `.dmg` copied to a SECOND Mac with no Python / no Qt installed mounts, and the app launches to a working tray icon with no Gatekeeper warning and no runtime prerequisites.
