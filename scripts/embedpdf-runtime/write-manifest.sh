#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ARTIFACT_DIR="${PDF_RUNTIME_ARTIFACT_DIR:-$SOURCE_DIR/out/embedpdf-runtime-artifacts}"
RELEASE_BASE_URL="${PDF_RUNTIME_RELEASE_BASE_URL:-}"
OUT_FILE="${1:-$ARTIFACT_DIR/pdf-runtime-build.generated.json}"
SHA="$(git -C "$SOURCE_DIR" rev-parse HEAD)"

if [[ -z "$RELEASE_BASE_URL" ]]; then
  echo "PDF_RUNTIME_RELEASE_BASE_URL is required" >&2
  exit 1
fi

node - <<'NODE' "$ARTIFACT_DIR" "$RELEASE_BASE_URL" "$OUT_FILE" "$SHA"
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const [artifactDir, releaseBaseUrl, outFile, sha] = process.argv.slice(2);
const artifacts = {};

for (const file of fs.readdirSync(artifactDir)) {
  const match = file.match(/^libembedpdf-pdf-runtime-(.+)-[a-f0-9]+\.tar\.gz$/);
  if (!match) continue;
  const target = match[1];
  const full = path.join(artifactDir, file);
  const sha256 = crypto.createHash('sha256').update(fs.readFileSync(full)).digest('hex');
  artifacts[target] = {
    url: `${releaseBaseUrl.replace(/\/$/, '')}/${file}`,
    sha256,
  };
}

fs.writeFileSync(
  outFile,
  JSON.stringify({ fork: 'embedpdf/pdfium', sha, artifacts }, null, 2) + '\n',
);
NODE

echo "$OUT_FILE"
