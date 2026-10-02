#!/usr/bin/env python3
"""Generate JSON field-name folding from the pinned Unicode 17.0.0 UCD archive."""
from __future__ import annotations

import argparse
import hashlib
import subprocess
from check_format import expected_version, formatter_version
from pathlib import Path
import zipfile

from generate_unicode_tables import UCD_SHA256, UCD_VERSION


def generate(archive: Path) -> str:
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != UCD_SHA256:
        raise ValueError("UCD archive does not match tools/toolchain.lock")
    with zipfile.ZipFile(archive) as zipped:
        text = zipped.read("CaseFolding.txt").decode("utf-8")
    parent: dict[int, int] = {}

    def root(scalar: int) -> int:
        parent.setdefault(scalar, scalar)
        if parent[scalar] != scalar:
            parent[scalar] = root(parent[scalar])
        return parent[scalar]

    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        source, status, target, *_ = [part.strip() for part in line.split(";")]
        if status not in {"C", "S"}:
            continue
        left, right = root(int(source, 16)), root(int(target, 16))
        parent[max(left, right)] = min(left, right)
    rows = "\n".join(
        f"    {{UINT32_C(0x{scalar:x}), UINT32_C(0x{root(scalar):x})}},"
        for scalar in sorted(parent) if scalar != root(scalar)
    )
    return f'''/* Generated from Unicode {UCD_VERSION} CaseFolding.txt; do not edit.
 * UCD SHA-256: {UCD_SHA256}
 * Only common and simple mappings participate. Turkic and full mappings are excluded. */
#include "r_library_json_internal.h"

typedef struct RJsonFoldPair {{ uint32_t scalar; uint32_t canonical; }} RJsonFoldPair;
static const RJsonFoldPair r_json_fold_pairs[] = {{
{rows}
}};
uint32_t r_json_simple_fold(uint32_t scalar) {{
    size_t low = 0U;
    size_t high = sizeof(r_json_fold_pairs) / sizeof(r_json_fold_pairs[0]);
    while (low < high) {{
        size_t middle = low + (high - low) / 2U;
        if (r_json_fold_pairs[middle].scalar < scalar) low = middle + 1U;
        else if (r_json_fold_pairs[middle].scalar > scalar) high = middle;
        else return r_json_fold_pairs[middle].canonical;
    }}
    return scalar;
}}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ucd-zip", type=Path, required=True)
    parser.add_argument("--source", type=Path,
                        default=Path("library/internal/json/source/fold_data.c"))
    parser.add_argument("--clang-format", default="clang-format")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if formatter_version(args.clang_format) != expected_version(root):
        raise ValueError("JSON table generation requires the pinned clang-format version")
    formatted = subprocess.run([args.clang_format, f"--style=file:{root / '.clang-format'}"],
                               input=generate(args.ucd_zip), text=True, capture_output=True, check=True)
    args.source.write_text(formatted.stdout, encoding="utf-8")


if __name__ == "__main__":
    main()
