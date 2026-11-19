#!/usr/bin/env bash
# Ribalta updater: checks the latest GitHub release and, if newer, rebuilds and reinstalls it.
# Run via `ribalta update`:  update.sh --prefix P --current V [--check] [-y|--yes]
# Exit codes: --check 0 up to date / 10 update available / 1 error; update 0 done, up to date or
# cancelled / 1 error. Your settings and statistics are never touched.
# The whole body is a function: install.sh overwrites this very file during an update, and bash
# must have parsed everything before running it.
set -euo pipefail

main() {
  local PREFIX="" CURRENT="" CHECK=0 YES=0
  local API="${RIBALTA_UPDATE_API:-https://api.github.com/repos/Ginteaa/Ribalta/releases/latest}"
  local TARBALL_TPL="${RIBALTA_UPDATE_TARBALL:-}"
  [ -n "$TARBALL_TPL" ] || TARBALL_TPL='https://github.com/Ginteaa/Ribalta/archive/refs/tags/{tag}.tar.gz'

  local RST="" BOLD="" DIM="" RED="" GRN="" YEL="" ORG=""
  if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != dumb ]; then
    RST=$'\033[0m'; BOLD=$'\033[1m'; DIM=$'\033[38;5;240m'; RED=$'\033[31m'
    GRN=$'\033[32m'; YEL=$'\033[33m'; ORG=$'\033[38;5;215m'
  fi
  ok()   { printf '%s✓%s %s\n' "$GRN" "$RST" "$1"; }
  fail() { printf '%s✗%s %s\n' "$RED" "$RST" "$1" >&2; }

  usage() {
    cat <<USAGE
Usage: ribalta update [--check] [-y|--yes]
  --check     only check: exit 10 if a newer release exists, 0 if up to date
  -y, --yes   do not ask for confirmation
  -h, --help  show this help
USAGE
  }

  while [ $# -gt 0 ]; do
    case "$1" in
      --prefix) [ $# -ge 2 ] || { usage >&2; return 1; }; PREFIX="$2"; shift 2 ;;
      --current) [ $# -ge 2 ] || { usage >&2; return 1; }; CURRENT="$2"; shift 2 ;;
      --check) CHECK=1; shift ;;
      -y|--yes) YES=1; shift ;;
      -h|--help) usage; return 0 ;;
      *) usage >&2; return 1 ;;
    esac
  done
  if [ -z "$PREFIX" ] || [ -z "$CURRENT" ]; then usage >&2; return 1; fi

  printf '\n  %s┌───┐%s %sRibalta%s %s· update%s\n\n' "$DIM" "$RST" "$BOLD$ORG" "$RST" "$DIM" "$RST"

  fetch() {                   # fetch <url> <output file>
    if command -v curl >/dev/null 2>&1; then
      curl -fsSL --connect-timeout 10 --max-time 300 -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
      wget -q -T 30 -O "$2" "$1"
    else
      fail "Neither curl nor wget is available."; return 1
    fi
  }

  TMP="$(mktemp -d "${TMPDIR:-/tmp}/ribalta-update.XXXXXX")"
  trap 'rm -rf "$TMP"' EXIT

  # ---- latest release ----------------------------------------------------------------------
  printf '%sChecking for updates%s\n' "$BOLD" "$RST"
  if ! fetch "$API" "$TMP/release.json"; then
    fail "Could not download the release information from $API"; return 1
  fi
  local TAG="" BODY=""
  if command -v jq >/dev/null 2>&1; then
    TAG="$(jq -r '.tag_name // empty' "$TMP/release.json" 2>/dev/null || true)"
    BODY="$(jq -r '.body // empty' "$TMP/release.json" 2>/dev/null || true)"
  else
    local flat; flat="$(tr -d '\n' <"$TMP/release.json")"
    TAG="$(printf '%s' "$flat" | sed -nE 's/.*"tag_name"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/p')"
    BODY="$(printf '%s' "$flat" \
      | sed -nE 's/.*"body"[[:space:]]*:[[:space:]]*"(([^"\\]|\\.)*)".*/\1/p' \
      | sed -e 's/\\\\/\x01/g' -e 's/\\"/"/g' -e 's/\\r//g' -e 's/\\t/\t/g' \
            -e 's/\\n/\n/g' -e 's/\x01/\\/g')"
  fi
  if ! [[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    fail "Unexpected release tag in the response: ${TAG:-<none>}"; return 1
  fi
  local LATEST="${TAG#v}"

  local newer=0
  if [ "$LATEST" != "$CURRENT" ] && [ "$(printf '%s\n%s\n' "$CURRENT" "$LATEST" | sort -V | tail -n 1)" = "$LATEST" ]; then
    newer=1
  fi

  print_notes() {
    if [ -n "$BODY" ]; then
      printf '\n%sRelease notes%s\n' "$BOLD" "$RST"
      printf '%s\n' "$BODY" | sed 's/^/    /'
      printf '\n'
    fi
  }

  if [ "$CHECK" = 1 ]; then
    printf '  Current  %s\n  Latest   %s\n' "$CURRENT" "$LATEST"
    if [ "$newer" = 1 ]; then
      printf '\n%sA new version is available.%s Run "ribalta update" to install it.\n' "$YEL" "$RST"
      print_notes
      return 10
    fi
    ok "Ribalta is up to date."
    return 0
  fi

  if [ "$newer" = 0 ]; then
    ok "Ribalta is up to date ($CURRENT)."
    return 0
  fi

  if [ ! -d "$PREFIX/bin" ] || [ ! -w "$PREFIX/bin" ]; then
    fail "$PREFIX/bin is not writable. Run the update as the user that installed Ribalta."
    return 1
  fi

  printf '  %s%s → %s%s\n' "$BOLD" "$CURRENT" "$LATEST" "$RST"
  print_notes

  if [ "$YES" = 0 ]; then
    if [ -t 0 ]; then
      local ans=""
      read -r -p "Update now? [Y/n] " ans || ans="n"
      case "$ans" in
        ""|y|Y|yes|YES|Yes) ;;
        *) printf 'Cancelled.\n'; return 0 ;;
      esac
    else
      fail "No terminal to ask for confirmation. Re-run with -y to update."
      return 1
    fi
  fi

  # ---- download and install ----------------------------------------------------------------
  local url="${TARBALL_TPL//\{tag\}/$TAG}"
  printf '%sDownloading%s %s\n' "$BOLD" "$RST" "$TAG"
  if ! fetch "$url" "$TMP/src.tar.gz"; then
    fail "Download failed: $url"; return 1
  fi
  mkdir "$TMP/src"
  if ! tar -xzf "$TMP/src.tar.gz" -C "$TMP/src"; then
    fail "The downloaded archive could not be extracted."; return 1
  fi
  local dirs=("$TMP/src"/*/)
  if [ "${#dirs[@]}" -ne 1 ] || [ ! -d "${dirs[0]}" ]; then
    fail "Unexpected archive layout."; return 1
  fi
  local SRC="${dirs[0]%/}"
  if [ ! -f "$SRC/tools/install.sh" ] || [ ! -f "$SRC/VERSION" ]; then
    fail "The archive does not look like a Ribalta source tree."; return 1
  fi

  bash "$SRC/tools/install.sh" "$PREFIX" || { fail "Installation failed."; return 1; }

  if pgrep -x ribalta >/dev/null 2>&1; then
    printf '%s!%s Restart Ribalta to use the new version.\n' "$YEL" "$RST"
  fi
  return 0
}

main "$@"; exit $?   # one line: parsed whole before install.sh replaces this file
