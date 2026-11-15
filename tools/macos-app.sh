#!/usr/bin/env bash
# Builds build/Ribalta.app from the already-built ./ribalta binary (normally run via `make app`).
# Env: BUNDLE_LIBS=1  copy Homebrew dylibs into the bundle (needs dylibbundler)
#      MACOS_APP_DRYRUN=1  test the layout on a non-macOS machine (skips iconutil/strip/codesign)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

DRY="${MACOS_APP_DRYRUN:-0}"
if [ "$(uname -s)" != Darwin ] && [ "$DRY" != 1 ]; then
  echo "macos-app: this script only runs on macOS" >&2; exit 1
fi
[ -x ./ribalta ] || { echo "macos-app: ./ribalta not found, run 'make release' first" >&2; exit 1; }

# versione: tag git, altrimenti quella del metainfo, altrimenti 0.5.0
VERSION="$(git describe --tags --always 2>/dev/null || true)"
VERSION="${VERSION#v}"
# CFBundleVersion accetta solo cifre e punti: se non e' un tag pulito si usa il metainfo
if ! [[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]]; then
  VERSION="$(sed -n 's/.*<release version="\([0-9.]*\)".*/\1/p' \
    packaging/flatpak/io.github.ginteaa.Ribalta.metainfo.xml 2>/dev/null | head -n1)"
  VERSION="${VERSION:-0.5.0}"
fi

APP=build/Ribalta.app
say()  { printf '  %s\n' "$*"; }
warn() { printf '  warning: %s\n' "$*" >&2; }
skip() { say "skipped (dry run): $*"; }

echo "Building $APP (v$VERSION)"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets"

cp ribalta "$APP/Contents/MacOS/ribalta"
sed "s/@VERSION@/$VERSION/g" packaging/macos/Info.plist > "$APP/Contents/Info.plist"
for f in font.ttf OFL.txt ding.wav ribalta.svg; do
  cp "assets/$f" "$APP/Contents/Resources/assets/$f"
done
say "bundle layout ready"

# ---- icona ---------------------------------------------------------------------------------
if [ "$DRY" = 1 ] || ! command -v iconutil >/dev/null 2>&1; then
  [ "$DRY" = 1 ] && skip "icon" || warn "iconutil not found, bundle has no icon"
else
  ICONSET="$(mktemp -d)/Ribalta.iconset"
  mkdir -p "$ICONSET"
  I=assets/icons
  cp "$I/16x16.png"   "$ICONSET/icon_16x16.png"
  cp "$I/32x32.png"   "$ICONSET/icon_16x16@2x.png"
  cp "$I/32x32.png"   "$ICONSET/icon_32x32.png"
  cp "$I/64x64.png"   "$ICONSET/icon_32x32@2x.png"
  cp "$I/128x128.png" "$ICONSET/icon_128x128.png"
  cp "$I/256x256.png" "$ICONSET/icon_128x128@2x.png"
  cp "$I/256x256.png" "$ICONSET/icon_256x256.png"
  cp "$I/512x512.png" "$ICONSET/icon_256x256@2x.png"
  cp "$I/512x512.png" "$ICONSET/icon_512x512.png"
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/Ribalta.icns"
  rm -rf "$(dirname "$ICONSET")"
  say "icon built"
fi

# ---- strip ---------------------------------------------------------------------------------
if [ "$DRY" = 1 ]; then skip "strip"
elif command -v strip >/dev/null 2>&1; then strip -x "$APP/Contents/MacOS/ribalta"; say "binary stripped"
fi

# ---- librerie incluse ----------------------------------------------------------------------
if [ "${BUNDLE_LIBS:-0}" = 1 ]; then
  if [ "$DRY" = 1 ]; then skip "dylibbundler"
  else
    command -v dylibbundler >/dev/null 2>&1 \
      || { echo "macos-app: BUNDLE_LIBS=1 needs dylibbundler (brew install dylibbundler)" >&2; exit 1; }
    dylibbundler -od -b -x "$APP/Contents/MacOS/ribalta" \
      -d "$APP/Contents/Frameworks" -p @executable_path/../Frameworks/ >/dev/null
    say "libraries bundled"
  fi
fi

# ---- firma ad-hoc (obbligatoria su Apple Silicon dopo install_name_tool) -------------------
if [ "$DRY" = 1 ]; then skip "codesign"
else
  codesign --force --deep --sign - "$APP"
  codesign --verify "$APP"
  say "signed (ad-hoc)"
fi

echo "Done: $APP"
