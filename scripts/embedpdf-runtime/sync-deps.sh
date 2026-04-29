#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
WORKSPACE_DIR="$(cd "$SOURCE_DIR/.." && pwd)"
SOLUTION_NAME="$(basename "$SOURCE_DIR")"
FORK_URL="${PDF_RUNTIME_FORK_URL:-https://github.com/embedpdf/pdfium.git}"
REF="${PDF_RUNTIME_REF:-HEAD}"

cat > "$WORKSPACE_DIR/.gclient" <<EOF
solutions = [
  { "name": "$SOLUTION_NAME",
    "url":  "$FORK_URL",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
  },
]
EOF

(
  cd "$SOURCE_DIR"
  gclient sync -r "$REF" --no-history --shallow --nohooks --deps=builder
)
