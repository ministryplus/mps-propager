#!/usr/bin/env bash
#
# build.sh — configure → build → deploy → codesign → notarize → staple → .dmg
#
# Produces a signed, notarized, stapled ProPager.dmg that launches with NO
# runtime prerequisites on any Mac (arm64 + Intel).
#
# ────────────────────────────────────────────────────────────────────────────
# Qt MUST come from the official Qt online installer (LGPL), whose macOS
# binaries are universal2 (x86_64 + arm64). Homebrew Qt is SINGLE-ARCH and must
# NOT be used for release builds — a Homebrew-linked ProPager cannot be
# universal2 and will fail the `lipo -archs` check below.
# ────────────────────────────────────────────────────────────────────────────
#
# Prerequisites (provided by the operator, not the repo):
#   * A "Developer ID Application: Isaac Wiebe (S8KL27Z2X8)" certificate in the
#     login keychain  (override the identity via  DEV_ID=...).
#   * A stored notarytool credential profile named "NOTARY_PWD":
#         xcrun notarytool store-credentials NOTARY_PWD \
#             --apple-id "<you@example.com>" --team-id "<TEAMID>" \
#             --password "<app-specific-password>"
#     (override the profile name via  NOTARY_PROFILE=...).
#
# Usage:  ./build.sh
set -euo pipefail

# ── Tunables (override via env) ─────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QT_DIR="${QT_DIR:-$HOME/src/Qt/6.8.3/macos}"                        # online-installer Qt 6.8 LTS (universal2; minos 12.0)
CMAKE="${CMAKE:-$HOME/src/Qt/Tools/CMake/CMake.app/Contents/bin/cmake}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/release}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"
DEV_ID="${DEV_ID:-Developer ID Application: Isaac Wiebe (S8KL27Z2X8)}" # signing identity
NOTARY_PROFILE="${NOTARY_PROFILE:-NOTARY_PWD}"                      # stored notarytool keychain profile
ENTITLEMENTS="${ENTITLEMENTS:-$REPO_ROOT/entitlements.plist}"
APP_NAME="ProPager"
# DMG_PATH is derived after the build from the bundle's version (see step 3) so
# the version lives in one place — CMakeLists.txt's MACOSX_BUNDLE_* fields.

log()  { printf '\033[1;34m▸ %s\033[0m\n' "$*"; }
die()  { printf '\033[1;31m✗ %s\033[0m\n' "$*" >&2; exit 1; }

# ── Preflight ────────────────────────────────────────────────────────────────
[ -x "$CMAKE" ]   || die "cmake not found at $CMAKE (set CMAKE=...)"
[ -d "$QT_DIR" ]  || die "Qt online-installer dir not found at $QT_DIR (set QT_DIR=...)"
[ -f "$ENTITLEMENTS" ] || die "entitlements.plist not found at $ENTITLEMENTS"
security find-identity -v -p codesigning | grep -q "$DEV_ID" \
    || die "no codesigning identity matching '$DEV_ID' in the keychain"
xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 \
    || die "notary profile '$NOTARY_PROFILE' not stored (see header for store-credentials)"

# ── 1. Configure: Release, universal2, against online-installer Qt ───────────
log "Configuring (Release, universal2)…"
"$CMAKE" -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_INSTALL_PREFIX="$DIST_DIR"

# ── 2. Build ─────────────────────────────────────────────────────────────────
log "Building…"
"$CMAKE" --build "$BUILD_DIR" --target "$APP_NAME"

# ── 3. Deploy: run the CMake-generated deploy script (bundles Qt, fixes rpaths)
log "Deploying Qt frameworks/plugins into the .app…"
rm -rf "$DIST_DIR"
"$CMAKE" --install "$BUILD_DIR"

APP="$(/usr/bin/find "$DIST_DIR" -maxdepth 3 -name "${APP_NAME}.app" -type d | head -n1)"
[ -n "$APP" ] || die "deployed ${APP_NAME}.app not found under $DIST_DIR"
log "Deployed bundle: $APP"

# Name the .dmg with the bundle's version (single source of truth: CMakeLists).
VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' \
    "$APP/Contents/Info.plist" 2>/dev/null)" || die "could not read version from Info.plist"
DMG_PATH="$DIST_DIR/${APP_NAME}-${VERSION}.dmg"

# ── 4. Codesign inside-out: nested code FIRST, .app LAST ─────────────────────
#     Hardened runtime (--options runtime) + secure timestamp on everything.
#
#     entitlements.plist is an intentionally EMPTY dict — this native Qt6/C++
#     binary needs no extra capabilities. It deliberately omits the two
#     exceptions the OLD Python app required (allow-unsigned-executable-memory,
#     disable-library-validation): those existed only for the embedded, JITing
#     Python interpreter, and adding them back would weaken the hardened runtime
#     for no benefit. NOTE: keep entitlements.plist comment-free — codesign's
#     entitlements parser (AMFIUnserializeXML) rejects XML comments with a
#     "syntax error" even though `plutil -lint` accepts them.
sign() { codesign --force --options runtime --timestamp \
                  --entitlements "$ENTITLEMENTS" --sign "$DEV_ID" "$@"; }

log "Signing nested frameworks…"
while IFS= read -r fw; do sign "$fw"; done < <(
    /usr/bin/find "$APP/Contents/Frameworks" -name "*.framework" -type d 2>/dev/null)

log "Signing nested plugins / dylibs…"
while IFS= read -r lib; do sign "$lib"; done < <(
    /usr/bin/find "$APP/Contents" \( -name "*.dylib" -o -name "*.so" \) -type f 2>/dev/null)

log "Signing the .app (last)…"
sign "$APP"

# ── 5. Notarize (submit + wait) ──────────────────────────────────────────────
log "Notarizing (submitting a zip and waiting)…"
NOTARIZE_ZIP="$DIST_DIR/${APP_NAME}-notarize.zip"
/usr/bin/ditto -c -k --keepParent "$APP" "$NOTARIZE_ZIP"
xcrun notarytool submit "$NOTARIZE_ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
rm -f "$NOTARIZE_ZIP"

# ── 6. Staple the .app ───────────────────────────────────────────────────────
log "Stapling the .app…"
xcrun stapler staple "$APP"

# ── 7. Build the .dmg ────────────────────────────────────────────────────────
log "Building the .dmg…"
rm -f "$DMG_PATH"
/usr/bin/hdiutil create -volname "$APP_NAME" -srcfolder "$APP" \
    -ov -format UDZO "$DMG_PATH"

# ── 8. Notarize the .dmg, then staple it (staple BOTH the .app and the .dmg) ─
#     The zip submitted in step 5 only gets the .app a ticket; the .dmg is a
#     distinct artifact with its own hash and needs its OWN notarization or the
#     staple fails with "Record not found" (Apple has no ticket for the dmg).
#     The nested .app is already signed+stapled, so this pass is quick.
log "Notarizing the .dmg…"
xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait

log "Stapling the .dmg…"
xcrun stapler staple "$DMG_PATH"

# ── Verification ─────────────────────────────────────────────────────────────
log "Verifying…"
codesign --verify --deep --strict --verbose=2 "$APP"
spctl -a -vvv "$APP"
xcrun stapler validate "$APP"
xcrun stapler validate "$DMG_PATH"
lipo -archs "$APP/Contents/MacOS/$APP_NAME"

printf '\033[1;32m✓ Done: %s\033[0m\n' "$DMG_PATH"
