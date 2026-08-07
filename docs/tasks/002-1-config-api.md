# Task 002-1: Config Write + Reload + Validate API

**Spec:** [002 — Connections Tab](../specs/002-connections-tab.md)
**Status:** Pending
**Parallel group:** Wave 1 (parallel with 002-2, 002-3 — disjoint files)
**Depends on:** — (none)
**Blocks:** [002-4](002-4-connections-tab.md), [002-6](002-6-tray-reconnect-warning.md), [002-7](002-7-wiring-startup-gate.md)

## What

Turn `Config` from a read-only view of the `.ini` into an app-owned, writable, self-validating layer (Spec 002 Decisions 2 & 6; Implementation step 1). Add a per-key write API mirroring the existing typed getters (`setSlackBotToken`, `setSlackAppToken`, `setSlackListenChannel`, `setSlackIgnoreNumbers`, `setPropresenterHost`, `setPropresenterPort`, `setBatchWaitTime`, `setBatchMaxCount`, `setExpireTime`), each writing through `QSettings::setValue` + `sync()`; a `reload()` that re-reads settings from disk into memory (`QSettings::sync()`), so a Save's written values become the live values without constructing a new `Config`; and a `validate()` that returns a structured `ValidationResult` classifying the current settings into two tiers (Decision 6). `validate()` runs both on load and on every Save and produces the *same* structure that the tab renders inline and that the banner/tray surfaces consume (Decision 8). This replaces the current silent `intValueOr()` coercion of malformed ints with a *visible* shape warning — the getters keep coercing for runtime safety, but `validate()` reports the malformed value so the operator sees it.

**Explicitly kept (Decision 2):** `stripInlineComment()`, `templateContents()`, and `writeTemplateIfMissing()` stay — they still serve reading a legacy or hand-edited `.ini` on load. It is accepted and expected that the first in-app Save (via the new setters) collapses the commented template to a bare `key=value` file, because `QSettings` `IniFormat` does not preserve comments; the `.ini` is now app-owned. Do **not** delete the template or the strip helper.

## Steps

1. In `src/config.h`, define a `ValidationResult` type (a struct) with two tiers per Decision 6:
   - `requiredMissing` — a list of the required-but-unset keys among `slack/bot-token`, `slack/app-token`, `slack/listen-channel`. Presence here means "you must fix this to connect."
   - `shapeWarnings` — a list of present-but-malformed findings (key + human-readable message), meaning "this looks off" but does not block.
   Carry enough per-entry detail (the offending key and a display message) that the tab, banner, and tray can render without re-deriving it. Add `bool hasBlockingErrors() const` (true when `requiredMissing` is non-empty) and `bool isClean() const` for the surfaces to branch on.
2. Declare the per-key setters in `src/config.h`, one per existing getter, taking the same type the getter returns (`const QString&`, `int`, `const QStringList&`).
3. Declare `void reload();` (re-sync from disk) and `ValidationResult validate() const;` in `src/config.h`.
4. Implement the setters in `src/config.cpp`: each calls `m_settings->setValue(<key>, value)` using the same `k*` key constants the getters use (reuse them — do not hardcode key strings), then `m_settings->sync()` so the write hits disk immediately. Use the existing `kSlackBotToken` … `kExpireTime` constants.
5. Implement `reload()` as `m_settings->sync()` (which both flushes pending writes and re-reads the backing file), so subsequent getter calls reflect the on-disk state.
6. Implement `validate()` in `src/config.cpp`:
   - **Required-but-unset (Tier 1):** for `slack/bot-token`, `slack/app-token`, `slack/listen-channel`, add to `requiredMissing` when the trimmed value (via the existing getters / `stripInlineComment`) is empty.
   - **Present-but-malformed (Tier 2), warn-only:**
     - `slack/bot-token` present but does not start with `xoxb-`
     - `slack/app-token` present but does not start with `xapp-`
     - `slack/listen-channel` present but does not look like a Slack ID (starts with `C`)
     - `propresenter/port` present but outside `1–65535`
     - `propresenter/batch-wait-time`, `propresenter/batch-max-count`, `propresenter/expire-time` present but not a positive integer
   - `propresenter/host` and `propresenter/port` are **optional-with-default** (Spec 001 defaults `127.0.0.1` / `55184`): absence is fine and must **not** warn; only a *present-but-invalid* value warns. To distinguish "absent" from "present-but-invalid" without the defaulting getters hiding it, read the raw `QVariant` (as `intValueOr` does) and only run the shape check when the key is valid/non-empty.
7. Keep `intValueOr()`, `stripInlineComment()`, `templateContents()`, and `writeTemplateIfMissing()` unchanged; `validate()` reuses `stripInlineComment` semantics for its own reads so a hand-annotated legacy file validates the same as a bare one.
8. Extend `tests/tst_config.cpp` (same `QTemporaryDir`-per-test convention, explicit `.ini` path) with new `private slots`:
   - `setters_roundTripThroughReload` — construct a `Config` on a temp path, call each setter, `reload()`, and confirm every getter reads the written value back (including `slackIgnoreNumbers` as a `QStringList`).
   - `setters_firstSaveCollapsesTemplateToBareKeys` — first run writes the commented template; after a setter Save + `reload()`, the on-disk file is bare `key=value` (Decision 2 — assert the written key line has no leading `;` and the value round-trips).
   - `validate_flagsEachRequiredMissing` — with the three Slack required keys unset, `validate().requiredMissing` contains all three and `hasBlockingErrors()` is true; setting all three non-empty clears them.
   - `validate_shapeWarningsFireForMalformed` — set `bot-token=nope`, `app-token=nope`, `listen-channel=X`, `port=70000`, `expire-time=-1`; assert a `shapeWarnings` entry for each.
   - `validate_silentForValidAndAbsentDefaults` — set valid `xoxb-`/`xapp-`/`C…` tokens + a valid port; leave `host`/`port` **unset**; assert `shapeWarnings` is empty (absence of optional-with-default keys does not warn) and, with the three required keys set, `isClean()` is true.

## Acceptance Criteria

- [ ] `src/config.h` declares a `ValidationResult` struct with `requiredMissing` and `shapeWarnings` (each entry carrying key + display message), plus `hasBlockingErrors()` and `isClean()`.
- [ ] A setter exists for every Decision-4/10 key, each writing via `QSettings::setValue` + `sync()` and reusing the existing `k*` key constants (no duplicated key strings).
- [ ] `reload()` re-reads the `.ini` so a written value is observed by the getters without constructing a new `Config`.
- [ ] `validate()` returns all three Slack keys in `requiredMissing` when unset, and none when set.
- [ ] `validate()` emits a shape warning for each malformed value (bad `xoxb-`/`xapp-` prefix, non-`C` channel, out-of-range port, non-positive timing) and stays silent for valid values.
- [ ] `propresenter/host` and `propresenter/port` **absent** produce no warning; only present-but-invalid warns (replacing the old silent `intValueOr` coercion with a visible warning while the getter still coerces for runtime safety).
- [ ] `stripInlineComment()`, `templateContents()`, and `writeTemplateIfMissing()` are unchanged and still used on load; a legacy hand-annotated `.ini` still reads and validates correctly.
- [ ] The first in-app setter Save collapses the commented template to bare `key=value` (Decision 2) — asserted, not incidental.
- [ ] `tests/tst_config.cpp` gains the five new cases above and the full suite passes; the eight pre-existing cases still pass.

## Files Changed

| File | Action |
|---|---|
| src/config.h | Modify |
| src/config.cpp | Modify |
| tests/tst_config.cpp | Modify |

## Verification

1. Build the project and the `tst_config` target (per Task 001-1 conventions / the existing `enable_testing()` wiring in `CMakeLists.txt`).
2. Run `ctest` (or the `tst_config` binary directly) and confirm all cases pass — the eight original plus the five new setter/validate cases.
3. Manually confirm the round-trip: in a scratch `.ini`, call a setter, `reload()`, read the getter back — the value survives, and the file is bare `key=value` (no template comments) after the first Save.
4. Confirm `validate()` on an empty config reports exactly the three required Slack keys as missing and nothing else; on a fully valid config `isClean()` is true.
5. Confirm a malformed `port=70000` or `bot-token=nope` yields a shape warning while the corresponding getter still returns a usable (coerced/raw) value — the warning is additive, not a behavior change to the getters.
