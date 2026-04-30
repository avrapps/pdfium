#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
TARGET="${1:-}"
PDF_IS_COMPLETE_LIB=true

if [[ -z "$TARGET" ]]; then
  echo "usage: $0 <target>" >&2
  exit 1
fi

case "$TARGET" in
  wasm32)
    GN_TARGET_OS="emscripten"
    GN_TARGET_CPU="wasm"
    EXTRA_ARGS=$'\nis_clang=false\nuse_custom_libcxx=false'
    ;;
  darwin-arm64)
    GN_TARGET_OS="mac"
    GN_TARGET_CPU="arm64"
    ;;
  darwin-x64)
    GN_TARGET_OS="mac"
    GN_TARGET_CPU="x64"
    ;;
  linux-x64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="x64"
    ;;
  linux-arm64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="arm64"
    ;;
  linuxmusl-x64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="x64"
    EXTRA_ARGS=$'\nis_musl=true\nis_clang=false\nuse_sysroot=false\nuse_custom_libcxx=false\nuse_custom_libcxx_for_host=false\nuse_glib=false'
    ;;
  linuxmusl-arm64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="arm64"
    EXTRA_ARGS=$'\nis_musl=true\nis_clang=false\nuse_sysroot=false\nuse_custom_libcxx=false\nuse_custom_libcxx_for_host=false\nuse_glib=false'
    ;;
  win32-x64)
    GN_TARGET_OS="win"
    GN_TARGET_CPU="x64"
    PDF_IS_COMPLETE_LIB=false
    ;;
  win32-arm64)
    GN_TARGET_OS="win"
    GN_TARGET_CPU="arm64"
    PDF_IS_COMPLETE_LIB=false
    ;;
  *)
    echo "unknown target: $TARGET" >&2
    exit 1
    ;;
esac

PDF_RUNTIME_TARGET_OS_LIST="${PDF_RUNTIME_TARGET_OS_LIST:-$GN_TARGET_OS}" \
  "$SOURCE_DIR/scripts/embedpdf-runtime/ensure-deps.sh"

"$SOURCE_DIR/scripts/embedpdf-runtime/apply-patches.sh" "$TARGET"

if [[ "$TARGET" == linux-* ]]; then
  (
    cd "$SOURCE_DIR"
    build/install-build-deps.sh --no-prompt
    build/linux/sysroot_scripts/install-sysroot.py "--arch=$GN_TARGET_CPU"
  )
fi

OUT="$SOURCE_DIR/out/embedpdf-runtime/$TARGET"
mkdir -p "$OUT"

cat > "$OUT/args.gn" <<EOF
is_debug=false
treat_warnings_as_errors=false
pdf_use_skia=false
pdf_enable_xfa=false
pdf_enable_v8=false
is_component_build=false
clang_use_chrome_plugins=false
pdf_is_standalone=true
use_debug_fission=false
pdf_is_complete_lib=$PDF_IS_COMPLETE_LIB
pdf_use_partition_alloc=false
symbol_level=0
target_os="$GN_TARGET_OS"
target_cpu="$GN_TARGET_CPU"${EXTRA_ARGS:-}
EOF

(
  cd "$SOURCE_DIR"
  gn gen "$OUT"
  ninja -C "$OUT" pdfium
)

echo "$OUT"
