#!/usr/bin/env bash
# Signs DwgChecker.app with hardened runtime (nested dwgread first: notarization
# rejects any unsigned nested executable), then notarizes and staples it.
#
#   MACOS_CERT_P12 (base64), MACOS_CERT_PASSWORD, MACOS_SIGN_IDENTITY
#   ("Developer ID Application: Name (TEAMID)"), APPLE_ID, APPLE_TEAM_ID,
#   APPLE_APP_PASSWORD (app-specific password)
set -euo pipefail
APP="${1:?usage: sign_and_notarize.sh path/to/DwgChecker.app}"
: "${MACOS_SIGN_IDENTITY:?}"

# Temporary keychain with the Developer ID certificate.
KEYCHAIN="$RUNNER_TEMP/signing.keychain-db"
KEYPASS="$(uuidgen)"
echo "$MACOS_CERT_P12" | base64 --decode > "$RUNNER_TEMP/cert.p12"
security create-keychain -p "$KEYPASS" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYPASS" "$KEYCHAIN"
security import "$RUNNER_TEMP/cert.p12" -P "$MACOS_CERT_PASSWORD" -A -t cert -f pkcs12 -k "$KEYCHAIN"
security set-key-partition-list -S apple-tool:,apple: -k "$KEYPASS" "$KEYCHAIN" >/dev/null
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')

sign() { codesign --force --timestamp --options runtime --sign "$MACOS_SIGN_IDENTITY" "$@"; }

# 1. Nested helper executable.
sign "$APP/Contents/Helpers/dwgread"
# 2. Qt frameworks and plugins deployed by macdeployqt.
find "$APP/Contents/PlugIns" -name '*.dylib' -print0 2>/dev/null | while IFS= read -r -d '' f; do sign "$f"; done
find "$APP/Contents/Frameworks" -maxdepth 1 \( -name '*.framework' -o -name '*.dylib' \) -print0 2>/dev/null |
  while IFS= read -r -d '' f; do sign "$f"; done
# 3. The bundle itself, last.
sign "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

# Notarize and staple.
ZIP="$RUNNER_TEMP/DwgChecker-notarize.zip"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
xcrun stapler staple "$APP"
spctl --assess --type execute --verbose "$APP"
