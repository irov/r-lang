#!/usr/bin/env python3
"""Generate the dependency-free Unicode 17 tables used by the R lexer."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import zipfile


UCD_VERSION = "17.0.0"
UCD_SHA256 = "2066d1909b2ea93916ce092da1c0ee4808ea3ef8407c94b4f14f5b7eb263d28e"


def parse_code_point_range(text: str) -> tuple[int, int]:
    fields = text.strip().split("..")
    first = int(fields[0], 16)
    last = int(fields[-1], 16)
    return first, last


def parse_derived_core_properties(text: str) -> tuple[list[tuple[int, int]], list[tuple[int, int]]]:
    properties: dict[str, list[tuple[int, int]]] = {"XID_Start": [], "XID_Continue": []}
    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        code_points, property_name = (field.strip() for field in line.split(";", 1))
        if property_name in properties:
            properties[property_name].append(parse_code_point_range(code_points))
    return properties["XID_Start"], properties["XID_Continue"]


def parse_exclusions(text: str) -> set[int]:
    result: set[int] = set()
    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        first, last = parse_code_point_range(line)
        result.update(range(first, last + 1))
    return result


def parse_unicode_data(
    text: str, exclusions: set[int]
) -> tuple[list[tuple[int, int]], list[tuple[int, list[int]]], list[tuple[int, int, int]]]:
    ccc: list[tuple[int, int]] = []
    decompositions: list[tuple[int, list[int]]] = []
    compositions: list[tuple[int, int, int]] = []

    for line in text.splitlines():
        fields = line.split(";")
        code_point = int(fields[0], 16)
        combining_class = int(fields[3], 10)
        decomposition_field = fields[5].strip()
        if combining_class != 0:
            ccc.append((code_point, combining_class))
        if decomposition_field and not decomposition_field.startswith("<"):
            decomposition = [int(value, 16) for value in decomposition_field.split()]
            decompositions.append((code_point, decomposition))
            if len(decomposition) == 2 and code_point not in exclusions:
                compositions.append((decomposition[0], decomposition[1], code_point))

    compositions.sort(key=lambda item: (item[0], item[1]))
    return ccc, decompositions, compositions


def format_rows(rows: list[str], indent: str = "    ") -> str:
    return "\n".join(f"{indent}{row}" for row in rows)


def make_header() -> str:
    return f"""/* Generated from Unicode {UCD_VERSION}; do not edit. */
#ifndef R_UNICODE_DATA_H
#define R_UNICODE_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct RUnicodeRange {{
    uint32_t first;
    uint32_t last;
}} RUnicodeRange;

typedef struct RUnicodeCombiningClass {{
    uint32_t code_point;
    uint8_t combining_class;
}} RUnicodeCombiningClass;

typedef struct RUnicodeDecomposition {{
    uint32_t code_point;
    uint32_t first_mapping;
    uint16_t mapping_length;
}} RUnicodeDecomposition;

typedef struct RUnicodeComposition {{
    uint32_t first;
    uint32_t second;
    uint32_t composite;
}} RUnicodeComposition;

extern const RUnicodeRange r_unicode_xid_start_ranges[];
extern const size_t r_unicode_xid_start_range_count;
extern const RUnicodeRange r_unicode_xid_continue_ranges[];
extern const size_t r_unicode_xid_continue_range_count;
extern const RUnicodeCombiningClass r_unicode_combining_classes[];
extern const size_t r_unicode_combining_class_count;
extern const RUnicodeDecomposition r_unicode_decompositions[];
extern const size_t r_unicode_decomposition_count;
extern const uint32_t r_unicode_decomposition_mappings[];
extern const size_t r_unicode_decomposition_mapping_count;
extern const RUnicodeComposition r_unicode_compositions[];
extern const size_t r_unicode_composition_count;

#endif
"""


def make_source(
    xid_start: list[tuple[int, int]],
    xid_continue: list[tuple[int, int]],
    ccc: list[tuple[int, int]],
    decompositions: list[tuple[int, list[int]]],
    compositions: list[tuple[int, int, int]],
) -> str:
    mappings: list[int] = []
    decomposition_rows: list[str] = []
    for code_point, mapping in decompositions:
        first_mapping = len(mappings)
        mappings.extend(mapping)
        decomposition_rows.append(
            f"{{ UINT32_C(0x{code_point:X}), UINT32_C({first_mapping}), UINT16_C({len(mapping)}) }},"
        )

    sections = [
        f"""/* Generated from Unicode {UCD_VERSION}; UCD.zip SHA-256 {UCD_SHA256}. */
#include "unicode_data.h"

const RUnicodeRange r_unicode_xid_start_ranges[] = {{
{format_rows([f'{{ UINT32_C(0x{first:X}), UINT32_C(0x{last:X}) }},' for first, last in xid_start])}
}};
const size_t r_unicode_xid_start_range_count =
    sizeof(r_unicode_xid_start_ranges) / sizeof(r_unicode_xid_start_ranges[0]);

const RUnicodeRange r_unicode_xid_continue_ranges[] = {{
{format_rows([f'{{ UINT32_C(0x{first:X}), UINT32_C(0x{last:X}) }},' for first, last in xid_continue])}
}};
const size_t r_unicode_xid_continue_range_count =
    sizeof(r_unicode_xid_continue_ranges) / sizeof(r_unicode_xid_continue_ranges[0]);

const RUnicodeCombiningClass r_unicode_combining_classes[] = {{
{format_rows([f'{{ UINT32_C(0x{code_point:X}), UINT8_C({combining_class}) }},' for code_point, combining_class in ccc])}
}};
const size_t r_unicode_combining_class_count =
    sizeof(r_unicode_combining_classes) / sizeof(r_unicode_combining_classes[0]);

const RUnicodeDecomposition r_unicode_decompositions[] = {{
{format_rows(decomposition_rows)}
}};
const size_t r_unicode_decomposition_count =
    sizeof(r_unicode_decompositions) / sizeof(r_unicode_decompositions[0]);

const uint32_t r_unicode_decomposition_mappings[] = {{
{format_rows([f'UINT32_C(0x{code_point:X}),' for code_point in mappings])}
}};
const size_t r_unicode_decomposition_mapping_count =
    sizeof(r_unicode_decomposition_mappings) / sizeof(r_unicode_decomposition_mappings[0]);

const RUnicodeComposition r_unicode_compositions[] = {{
{format_rows([f'{{ UINT32_C(0x{first:X}), UINT32_C(0x{second:X}), UINT32_C(0x{composite:X}) }},' for first, second, composite in compositions])}
}};
const size_t r_unicode_composition_count =
    sizeof(r_unicode_compositions) / sizeof(r_unicode_compositions[0]);
"""
    ]
    return "".join(sections)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ucd-zip", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    arguments = parser.parse_args()

    archive_bytes = arguments.ucd_zip.read_bytes()
    digest = hashlib.sha256(archive_bytes).hexdigest()
    if digest != UCD_SHA256:
        raise SystemExit(f"unexpected UCD.zip SHA-256: {digest}")

    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
        derived = archive.read("DerivedCoreProperties.txt").decode("utf-8")
        unicode_data = archive.read("UnicodeData.txt").decode("utf-8")
        exclusions = archive.read("CompositionExclusions.txt").decode("utf-8")

    xid_start, xid_continue = parse_derived_core_properties(derived)
    ccc, decompositions, compositions = parse_unicode_data(
        unicode_data, parse_exclusions(exclusions)
    )

    arguments.header.write_text(make_header(), encoding="utf-8", newline="\n")
    arguments.source.write_text(
        make_source(xid_start, xid_continue, ccc, decompositions, compositions),
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()

