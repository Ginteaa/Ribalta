#!/usr/bin/env bash
# Ribalta installer: builds the release binary and installs it under PREFIX.
# Usage: tools/install.sh [PREFIX]   (default: ~/.local)  -- normally run via `make install`
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
PREFIX="${1:-${PREFIX:-$HOME/.local}}"
CC_BIN="${CC:-cc}"

# ---- output mode: fancy only on a real terminal --------------------------------------------
FANCY=0
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != dumb ]; then FANCY=1; fi
if [ "$FANCY" = 1 ]; then
  RST=$'\033[0m'; BOLD=$'\033[1m'; DIM=$'\033[38;5;240m'; RED=$'\033[31m'
  GRN=$'\033[32m'; YEL=$'\033[33m'; CYN=$'\033[36m'; WHT=$'\033[1;97m'; ORG=$'\033[38;5;215m'
else
  RST=; BOLD=; DIM=; RED=; GRN=; YEL=; CYN=; WHT=; ORG=
fi

banner() {
  local word=RIBALTA i top="" cut="" bot="" mid1="" mid2=""
  for ((i = 0; i < ${#word}; i++)); do
    top+="${DIM}┌───┐${RST}"
    mid1+="${DIM}│${RST} ${WHT}${word:i:1}${RST} ${DIM}│${RST}"
    cut+="${DIM}├───┤${RST}"
    mid2+="${DIM}│   │${RST}"
    bot+="${DIM}└───┘${RST}"
  done
  local ver=""
  if [ -r VERSION ]; then ver="$(head -n 1 VERSION | tr -d '[:space:]')"; fi
  [ -n "$ver" ] || ver="$(git describe --always --dirty 2>/dev/null || true)"
  ver="${ver:-dev}"
  printf '\n'
  printf '  %s\n  %s\n  %s\n  %s\n  %s\n' "$top" "$mid1" "$cut" "$mid2" "$bot"
  printf '  %sA tiny flip clock & study timer%s\n' "$ORG" "$RST"
  printf '  %sv%s%s\n\n' "$DIM" "$ver" "$RST"
}

# ---- progress ------------------------------------------------------------------------------
STEPS=5
WEIGHTS=(60 5 10 15 10)       # share of the global bar per step (sums to 100)
BARW=20
BASE=0                        # percentage completed before the current step
STEP_NO=0; STEP_MSG=""

bar() {                       # bar <percent>
  local pct=$1 n i s=""
  n=$((pct * BARW / 100))
  for ((i = 0; i < BARW; i++)); do
    if ((i < n)); then s+="█"; else s+="░"; fi
  done
  printf '\r\033[K  %s[%s%s%s]%s %3d%%  %s…' "$DIM" "$GRN" "$s" "$DIM" "$RST" "$pct" "$STEP_MSG"
}

step_begin() {                # step_begin <message>
  STEP_NO=$((STEP_NO + 1)); STEP_MSG="$1"
  if [ "$FANCY" = 1 ]; then bar "$BASE"; else printf '[%d/%d] %s...\n' "$STEP_NO" "$STEPS" "$STEP_MSG"; fi
}
step_progress() {             # step_progress <done> <total>: advance inside the current step
  [ "$FANCY" = 1 ] || return 0
  bar $((BASE + WEIGHTS[STEP_NO - 1] * $1 / $2))
}
step_end() {
  BASE=$((BASE + WEIGHTS[STEP_NO - 1]))
  if [ "$FANCY" = 1 ]; then printf '\r\033[K'; fi
  printf '%s✓%s %s\n' "$GRN" "$RST" "$STEP_MSG"
}

ok()   { printf '%s✓%s %s\n' "$GRN" "$RST" "$1"; }
fail() { printf '%s✗%s %s\n' "$RED" "$RST" "$1"; }

# ---- pre-flight ----------------------------------------------------------------------------
banner
printf '%sChecking requirements%s\n' "$BOLD" "$RST"
missing=0
check() {                     # check <label> <command...>
  local label=$1; shift
  if "$@" >/dev/null 2>&1; then ok "$label"; else fail "$label"; missing=1; fi
}
check "C compiler ($CC_BIN)" command -v "$CC_BIN"
check "make" command -v make
check "pkg-config" command -v pkg-config
if command -v pkg-config >/dev/null 2>&1; then
  check "SDL3" pkg-config --exists sdl3
  check "SDL3_ttf" pkg-config --exists sdl3-ttf
  check "fontconfig" pkg-config --exists fontconfig
else
  fail "SDL3 (cannot check without pkg-config)"
  fail "SDL3_ttf (cannot check without pkg-config)"
  fail "fontconfig (cannot check without pkg-config)"
fi
if [ "$missing" = 1 ]; then
  printf '\n%sSome requirements are missing.%s\n' "$YEL" "$RST"
  printf '  Arch Linux:        sudo pacman -S base-devel sdl3 sdl3_ttf fontconfig\n'
  printf '  Debian / Ubuntu:   install a C compiler, make, pkg-config and the SDL3, SDL3_ttf\n'
  printf '                     (>= 3.2) and fontconfig (libfontconfig-dev) development packages\n'
  printf '  Fedora:            install a C compiler, make, pkgconf and the SDL3, SDL3_ttf\n'
  printf '                     (>= 3.2) and fontconfig (fontconfig-devel) development packages\n'
  exit 1
fi
printf '\n'

# ---- 1. build ------------------------------------------------------------------------------
# `make release` runs `clean`, which deletes build/: keep the log outside it and copy it at the end.
LOG="$(mktemp "${TMPDIR:-/tmp}/ribalta-install.XXXXXX")"
trap 'rm -f "$LOG"' EXIT
total_c=$(find src -name '*.c' | wc -l)
[ "$total_c" -gt 0 ] || total_c=1

step_begin "Building"
make release >"$LOG" 2>&1 &
pid=$!
while kill -0 "$pid" 2>/dev/null; do
  if [ "$FANCY" = 1 ]; then
    objs=$(find build -name '*.o' 2>/dev/null | wc -l)
    ((objs > total_c)) && objs=$total_c
    step_progress "$objs" $((total_c + 1))   # last slot is the link step
  fi
  sleep 0.1
done
rc=0; wait "$pid" || rc=$?
mkdir -p build
cp "$LOG" build/install.log
if [ "$rc" -ne 0 ]; then
  if [ "$FANCY" = 1 ]; then printf '\r\033[K'; fi
  fail "Build failed"
  printf '\n'; tail -n 15 "$LOG"
  printf '\nFull log: %s/build/install.log\n' "$PWD"
  exit 1
fi
step_end

# ---- 2. binary -----------------------------------------------------------------------------
step_begin "Installing binary"
install -Dm755 ribalta "$PREFIX/bin/ribalta"
step_end

# ---- 3. assets -----------------------------------------------------------------------------
step_begin "Installing assets"
n=0
for f in font.ttf OFL.txt ding.wav ribalta.svg; do
  install -Dm644 "assets/$f" "$PREFIX/share/ribalta/$f"
  n=$((n + 1)); step_progress "$n" 6
done
# the `ribalta update` / `ribalta uninstall` scripts
for f in update.sh uninstall.sh; do
  install -Dm755 "tools/$f" "$PREFIX/share/ribalta/$f"
  n=$((n + 1)); step_progress "$n" 6
done
step_end

# ---- 4. icons ------------------------------------------------------------------------------
step_begin "Installing icons"
sizes=(16 22 24 32 48 64 128 256 512)
n=0
for s in "${sizes[@]}"; do
  install -Dm644 "assets/icons/${s}x${s}.png" "$PREFIX/share/icons/hicolor/${s}x${s}/apps/ribalta.png"
  n=$((n + 1)); step_progress "$n" $((${#sizes[@]} + 1))
  [ "$FANCY" = 1 ] && sleep 0.03
done
install -Dm644 assets/ribalta.svg "$PREFIX/share/icons/hicolor/scalable/apps/ribalta.svg"
step_end

# ---- 5. desktop entry + icon cache ---------------------------------------------------------
step_begin "Desktop entry and icon cache"
install -Dm644 assets/ribalta.desktop "$PREFIX/share/applications/ribalta.desktop"
step_progress 1 2
if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -f -t "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
fi
step_end

# ---- summary -------------------------------------------------------------------------------
cfg="${XDG_CONFIG_HOME:-$HOME/.config}/ribalta/config.ini"
dat="${XDG_DATA_HOME:-$HOME/.local/share}/ribalta/sessions.csv"
printf '\n%sRibalta is installed.%s\n' "$BOLD" "$RST"
printf '  Location   %s\n' "$PREFIX"
printf '  Run it     %sribalta%s (or search "Ribalta" in your app launcher)\n' "$CYN" "$RST"
printf '  Settings   %s\n' "$cfg"
printf '  Statistics %s\n' "$dat"
printf '  Update     ribalta update\n'
printf '  Uninstall  ribalta uninstall (your settings and statistics are kept)\n'
case ":$PATH:" in
  *":$PREFIX/bin:"*) ;;
  *)
    printf '\n%s!%s %s%s/bin is not in your PATH.%s Add this to your shell config:\n' "$YEL" "$RST" "$YEL" "$PREFIX" "$RST"
    printf '    export PATH="%s/bin:$PATH"\n' "$PREFIX"
    ;;
esac
printf '\n'
