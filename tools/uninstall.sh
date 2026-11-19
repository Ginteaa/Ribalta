#!/usr/bin/env bash
# Ribalta uninstaller: removes only the files that install.sh put under PREFIX.
# Your settings and statistics are never touched.
# Usage: tools/uninstall.sh [PREFIX]   (default: ~/.local)  -- run via `make uninstall`, no prompt
#        uninstall.sh --prefix P --current V [-y|--yes]       -- run via `ribalta uninstall`
# The whole body is a function: this script deletes itself, and bash must have parsed it all first.
set -euo pipefail

main() {
  local prefix="" ask=0 yes=0
  local usage="Usage: ribalta uninstall [-y|--yes]"
  while [ $# -gt 0 ]; do
    case "$1" in
      --prefix)  [ $# -ge 2 ] || { echo "$usage" >&2; return 1; }; prefix="$2"; ask=1; shift 2 ;;
      --current) [ $# -ge 2 ] || { echo "$usage" >&2; return 1; }; shift 2 ;;
      -y|--yes)  yes=1; shift ;;
      -h|--help) echo "$usage"; return 0 ;;
      -*)        echo "$usage" >&2; return 1 ;;
      *)         prefix="$1"; shift ;;
    esac
  done
  PREFIX="${prefix:-${PREFIX:-$HOME/.local}}"

  local RST="" BOLD="" DIM="" RED="" GRN="" ORG=""
  if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != dumb ]; then
    RST=$'\033[0m'; BOLD=$'\033[1m'; DIM=$'\033[38;5;240m'; RED=$'\033[31m'
    GRN=$'\033[32m'; ORG=$'\033[38;5;215m'
  fi

  if [ "$ask" = 1 ] && [ "$yes" = 0 ]; then
    if [ -t 0 ]; then
      local ans=""
      read -r -p "Uninstall Ribalta from $PREFIX? Your settings and statistics are kept. [y/N] " ans || ans="n"
      case "$ans" in
        y|Y|yes|YES|Yes) ;;
        *) printf 'Cancelled.\n'; return 0 ;;
      esac
    else
      printf '%s✗%s No terminal to ask for confirmation. Re-run with -y to uninstall.\n' "$RED" "$RST" >&2
      return 1
    fi
  fi

  printf '\n  %s┌───┐%s %sRibalta%s %s· uninstall%s\n\n' "$DIM" "$RST" "$BOLD$ORG" "$RST" "$DIM" "$RST"

  local removed=0 absent=0
  remove() {                  # remove <file>
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
  local s f
  for s in 16 22 24 32 48 64 128 256 512; do
    remove "$PREFIX/share/icons/hicolor/${s}x${s}/apps/ribalta.png"
  done
  remove "$PREFIX/share/icons/hicolor/scalable/apps/ribalta.svg"
  # only our own files: this folder may also hold the user's sessions.csv
  for f in font.ttf OFL.txt ding.wav ribalta.svg update.sh uninstall.sh; do
    remove "$PREFIX/share/ribalta/$f"
  done
  rmdir "$PREFIX/share/ribalta" 2>/dev/null || true

  if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -f -t "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
  fi

  local cfg="${XDG_CONFIG_HOME:-$HOME/.config}/ribalta"
  local dat="${XDG_DATA_HOME:-$HOME/.local/share}/ribalta"
  printf '\n%sDone:%s %d removed, %d not found.\n' "$BOLD" "$RST" "$removed" "$absent"
  printf 'Your data was kept. To delete it too:\n'
  printf '  settings    %s\n' "$cfg"
  printf '  statistics  %s/sessions.csv\n' "$dat"
  printf '  e.g.  rm -r "%s" "%s/sessions.csv"\n\n' "$cfg" "$dat"
  return 0
}

main "$@"; exit $?   # one line: parsed whole before install.sh replaces this file
