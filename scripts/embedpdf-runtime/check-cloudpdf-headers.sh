#!/usr/bin/env bash
# Copyright 2026 CloudPDF LTD
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${SCRIPT_DIR}/cloudpdf-owned-source-files.txt"

status=0

if [[ ! -f "${MANIFEST}" ]]; then
  echo "Missing CloudPDF ownership manifest: ${MANIFEST}" >&2
  exit 1
fi

duplicate_paths="$({ sed -e '/^[[:space:]]*#/d' -e '/^[[:space:]]*$/d' "${MANIFEST}" || true; } | sort | uniq -d)"
if [[ -n "${duplicate_paths}" ]]; then
  echo "Duplicate paths in ${MANIFEST}:" >&2
  echo "${duplicate_paths}" >&2
  status=1
fi

while IFS= read -r relative_path; do
  [[ -z "${relative_path}" || "${relative_path}" == \#* ]] && continue

  source_path="${SOURCE_DIR}/${relative_path}"
  if [[ ! -f "${source_path}" ]]; then
    echo "Missing CloudPDF-owned file: ${relative_path}" >&2
    status=1
    continue
  fi

  header="$(sed -n '1,12p' "${source_path}")"
  notice_area="$(sed -n '1,32p' "${source_path}")"
  if ! grep -Fq 'CloudPDF LTD' <<<"${header}"; then
    echo "Missing CloudPDF LTD copyright header: ${relative_path}" >&2
    status=1
  fi
  if ! grep -Fq 'SPDX-License-Identifier: Apache-2.0' <<<"${header}"; then
    echo "Missing Apache-2.0 SPDX header: ${relative_path}" >&2
    status=1
  fi
  if grep -Eq 'Copyright ([0-9]{4} )?The (PDFium|EmbedPDF) Authors|Copyright PDFium Authors' <<<"${notice_area}"; then
    echo "Incorrect PDFium/EmbedPDF authorship in CloudPDF-owned file: ${relative_path}" >&2
    status=1
  fi
done <"${MANIFEST}"

if [[ "${status}" -ne 0 ]]; then
  exit "${status}"
fi

owned_count="$(sed -e '/^[[:space:]]*#/d' -e '/^[[:space:]]*$/d' "${MANIFEST}" | wc -l | tr -d ' ')"
echo "Verified ${owned_count} CloudPDF-owned source headers."
