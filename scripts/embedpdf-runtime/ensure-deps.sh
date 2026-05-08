#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
FORK_URL="${PDF_RUNTIME_FORK_URL:-https://github.com/embedpdf/pdfium.git}"
REF="${PDF_RUNTIME_REF:-HEAD}"
SYNC_MODE="${PDF_RUNTIME_SYNC:-auto}"
TARGET_OS_LIST="${PDF_RUNTIME_TARGET_OS_LIST:-}"
STAMP_DIR="$SOURCE_DIR/.embedpdf-runtime"
STAMP_FILE="$STAMP_DIR/deps-sync.stamp"

case "$SYNC_MODE" in
  auto | always | never | skip)
    ;;
  *)
    echo "PDF_RUNTIME_SYNC must be one of: auto, always, never, skip" >&2
    exit 1
    ;;
esac

deps_sha() {
  git -C "$SOURCE_DIR" hash-object "$SOURCE_DIR/DEPS"
}

required_paths_exist() {
  [[ -f "$SOURCE_DIR/build/config/BUILDCONFIG.gn" ]] &&
    [[ -d "$SOURCE_DIR/build/toolchain" ]] &&
    [[ -d "$SOURCE_DIR/third_party/llvm-build" ]]
}

write_stamp() {
  mkdir -p "$STAMP_DIR"
  cat > "$STAMP_FILE" <<EOF
fork_url=$FORK_URL
ref=$REF
target_os_list=$TARGET_OS_LIST
deps_sha=$(deps_sha)
EOF
}

stamp_matches() {
  [[ -f "$STAMP_FILE" ]] || return 1
  grep -Fxq "fork_url=$FORK_URL" "$STAMP_FILE" &&
    grep -Fxq "ref=$REF" "$STAMP_FILE" &&
    grep -Fxq "target_os_list=$TARGET_OS_LIST" "$STAMP_FILE" &&
    grep -Fxq "deps_sha=$(deps_sha)" "$STAMP_FILE"
}

needs_sync() {
  stamp_matches && required_paths_exist && return 1
  return 0
}

run_sync() {
  "$SOURCE_DIR/scripts/embedpdf-runtime/sync-deps.sh"
  if ! required_paths_exist; then
    echo "dependency sync completed, but required dependency paths are missing" >&2
    exit 1
  fi
  write_stamp
}

case "$SYNC_MODE" in
  always)
    run_sync
    ;;
  never)
    if needs_sync; then
      echo "dependencies are missing or stale, but PDF_RUNTIME_SYNC=never" >&2
      exit 1
    fi
    ;;
  skip)
    if ! required_paths_exist; then
      echo "dependencies are missing, but PDF_RUNTIME_SYNC=skip" >&2
      echo "run once with PDF_RUNTIME_SYNC=auto to fetch dependencies" >&2
      exit 1
    fi
    echo "Dependency sync skipped"
    ;;
  auto)
    if needs_sync; then
      run_sync
    else
      echo "Dependencies already synced"
    fi
    ;;
esac
