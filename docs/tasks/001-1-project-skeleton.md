# Task 001-1: Project Skeleton

**Spec:** [001 — ProPager Qt6/C++ Rewrite](../specs/001-propager-qt-rewrite.md)
**Status:** Pending
**Parallel group:** Wave 1 (solo — blocks everything)
**Depends on:** —
**Blocks:** all subsequent tasks

## What

Stand up the CMake + Qt6/C++ project skeleton that every later task builds on. This produces a buildable `MACOSX_BUNDLE` executable named **ProPager** whose `main.cpp` boots a `QApplication`, sets the organization/application identity so `QSettings` resolves correctly from day one (Decision 11), and installs a `QSystemTrayIcon` with a minimal (Quit) context menu. The app starts with no visible window — minimized-to-tray, matching the current app's behavior — and keeps running when its window is closed. Real icon assets, deploy, and signing are explicitly out of scope here (Task 8); a placeholder tray icon is acceptable.

## Steps

1. Create `CMakeLists.txt` at the repo root: set a project name (`ProPager`), require C++17 or newer, and enable `set(CMAKE_AUTOMOC ON)`.
2. In `CMakeLists.txt`, `find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets)` (Network + WebSockets declared now so later waves need no build-file changes) and link the executable against `Qt6::Widgets Qt6::Network Qt6::WebSockets`.
3. Declare the executable with `MACOSX_BUNDLE` and set bundle metadata: display name / window title `ProPager` and bundle identifier `com.isaacwiebe.propager` via `MACOSX_BUNDLE_GUI_IDENTIFIER` (and/or a bundle plist).
4. Create `src/main.cpp` instantiating `QApplication`.
5. In `main.cpp`, set `QCoreApplication::setOrganizationName("com.isaacwiebe")` and `QCoreApplication::setApplicationName("ProPager")` before any `QSettings` use, so the on-disk settings identity is correct from the start (Decision 11).
6. In `main.cpp`, create a `QSystemTrayIcon` (placeholder icon is fine) with a minimal `QMenu` context menu containing a **Quit** action wired to `QApplication::quit`; call `setVisible(true)` / `show()` on the tray icon.
7. Call `app.setQuitOnLastWindowClosed(false)` so closing/hiding the window keeps the tray (and the app) alive.
8. Start with no visible window (minimized-to-tray): do not `show()` any main window in this task.
9. Add a `build/` entry to `.gitignore`.

## Acceptance Criteria

- [ ] `CMakeLists.txt` exists, targets C++17+, has `CMAKE_AUTOMOC ON`, and does `find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets)`.
- [ ] The executable is built with `MACOSX_BUNDLE`; bundle display name / window title is `ProPager` and bundle identifier is `com.isaacwiebe.propager`.
- [ ] `src/main.cpp` sets organization name `com.isaacwiebe` and application name `ProPager` before any `QSettings` access.
- [ ] A `QSystemTrayIcon` appears with a context menu whose **Quit** action exits the app cleanly.
- [ ] `app.setQuitOnLastWindowClosed(false)` is set; no visible window is shown at launch (starts minimized to tray).
- [ ] `.gitignore` ignores `build/`.
- [ ] `cmake -B build && cmake --build build` succeeds from a clean checkout in a Qt6 dev environment.

## Files Changed

| File | Action |
|---|---|
| CMakeLists.txt | Create |
| src/main.cpp | Create |
| .gitignore | Modify |

## Verification

- Configure and build: `cmake -B build && cmake --build build` completes without errors.
- Launch the built `.app`/binary: a tray icon appears and **no** dock window is shown (minimized-to-tray).
- Open the tray context menu and choose **Quit**: the app exits cleanly (process terminates; no orphaned window/tray).
- Note: the app currently must be run from a Qt6 dev environment (official Qt installer per Decision 12). Bundling/deploy/signing so it runs with no runtime prerequisites is Task 8, not this task.
