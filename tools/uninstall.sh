#!/usr/bin/env bash
# Ribalta uninstaller: removes only the files that install.sh put under PREFIX.
# Your settings and statistics are never touched.
# Usage: tools/uninstall.sh [PREFIX]   (default: ~/.local)  -- normally run via `make uninstall`
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
PREFIX="${1:-${PREFIX:-$HOME/.local}}"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != dumb ]; then
  RST=$'\033[0m'; BOLD=$'\033[1m'; DIM=$'\033[38;5;240m'; GRN=$'\033[32m'; ORG=$'\033[38;5;215m'
else
  RST=; BOLD=; DIM=; GRN=; ORG=
fi

printf '\n  %s┌───┐%s %sRibalta%s %s· uninstall%s\n\n' "$DIM" "$RST" "$BOLD$ORG" "$RST" "$DIM" "$RST"

removed=0; absent=0
remove() {                    # remove <file>
  if [ -e "$1" ] || [ -L "$1" ]; then
    rm -f "$1"; removed=$((removed + 1))
    printf '%s✓%s removed   %s\n' "$GRN" "$RST" "$1"
  else
    absent=$((absent + 1))
    printf '%s-%s not found %s\n' "$DIM" "$RST" "$1"
  fi
}

remove "$PREFIX/bin/ribalta"
remove "$PREFIX/share/applications/ribalta.desktop"
for s in 16 22 24 32 48 64 128 256 512; do
  remove "$PREFIX/share/icons/hicolor/${s}x${s}/apps/ribalta.png"
done
remove "$PREFIX/share/icons/hicolor/scalable/apps/ribalta.svg"
# only our own files: this folder may also hold the user's sessions.csv
for f in font.ttf OFL.txt ding.wav ribalta.svg; do
  remove "$PREFIX/share/ribalta/$f"
done
rmdir "$PREFIX/share/ribalta" 2>/dev/null || true

if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -f -t "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
fi

cfg="${XDG_CONFIG_HOME:-$HOME/.config}/ribalta"
dat="${XDG_DATA_HOME:-$HOME/.local/share}/ribalta"
printf '\n%sDone:%s %d removed, %d not found.\n' "$BOLD" "$RST" "$removed" "$absent"
printf 'Your data was kept. To delete it too:\n'
printf '  settings    %s\n' "$cfg"
printf '  statistics  %s/sessions.csv\n' "$dat"
printf '  e.g.  rm -r "%s" "%s/sessions.csv"\n\n' "$cfg" "$dat"
