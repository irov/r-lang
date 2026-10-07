#!/usr/bin/env python3
"""Verify that the frontend coverage manifest matches normative Annex A."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ANNEX_START = "== Annex A — Normative EBNF grammar"
ANNEX_END = "== Annex B — Required diagnostics"
PRODUCTION_PATTERN = re.compile(r"^([a-z][a-z0-9-]*)\s*=", re.MULTILINE)
SECTION_PATTERN = re.compile(r"^=== (A\.[2-6])\b", re.MULTILINE)
EBNF_BLOCK_PATTERN = re.compile(r"^\[source,ebnf\]\s*\n----\n(.*?)\n----", re.MULTILINE | re.DOTALL)


def annex_data(specification: Path) -> tuple[str, str, list[dict[str, str]]]:
    source = specification.read_text(encoding="utf-8")
    start = source.find(ANNEX_START)
    end = source.find(ANNEX_END, start)
    if start < 0 or end < 0:
        raise ValueError("normative Annex A boundaries were not found")
    annex = source[start:end]
    digest = hashlib.sha256(annex.encode("utf-8")).hexdigest()
    productions: list[dict[str, str]] = []
    seen: set[str] = set()
    for block_match in EBNF_BLOCK_PATTERN.finditer(annex):
        preceding = annex[: block_match.start()]
        sections = list(SECTION_PATTERN.finditer(preceding))
        if not sections:
            continue
        section = sections[-1].group(1)
        for production_match in PRODUCTION_PATTERN.finditer(block_match.group(1)):
            name = production_match.group(1)
            if name in seen:
                raise ValueError(f"duplicate Annex A production: {name}")
            seen.add(name)
            productions.append({"name": name, "section": section})
    if not productions:
        raise ValueError("no Annex A productions were found")
    return annex, digest, productions


def generated_manifest(specification: Path) -> dict[str, object]:
    _annex, digest, productions = annex_data(specification)
    coverage_contracts = {
        "A.2": {
            "implementation": ["compiler/lexer/lexer.re", "compiler/lexer/lexer.c"],
            "tests": ["tests/frontend_tests.c:lexer"],
        },
        "A.3": {
            "implementation": ["compiler/parser/parser.c:declarations", "compiler/parser/static_conditions.inc"],
            "tests": ["tests/frontend_tests.c:valid_frontend", "tests/frontend_tests.c:static_conditions", "tests/frontend_tests.c:generic_syntax", "tests/frontend_tests.c:generic_recovery", "tests/frontend_tests.c:legacy_generic_diagnostic", "tests/frontend_tests.c:method_syntax", "tests/frontend_tests.c:method_recovery", "tests/frontend_tests.c:range_for_syntax", "tests/frontend_tests.c:range_for_recovery", "tests/frontend_tests.c:collection_syntax", "tests/frontend_tests.c:collection_recovery", "tests/fixtures/codegen_associated_types.r", "tests/fixtures/codegen_variadic.r", "tests/fixtures/codegen_expression_forms.r", "examples/generics/modules.map", "examples/methods/modules.map", "examples/reflection/modules.map", "examples/unzip/modules.map"],
        },
        "A.4": {
            "implementation": ["compiler/parser/parser.c:types"],
            "tests": ["tests/frontend_tests.c:valid_frontend", "tests/frontend_tests.c:static_conditions", "tests/frontend_tests.c:generic_syntax", "tests/frontend_tests.c:generic_recovery", "tests/frontend_tests.c:legacy_generic_diagnostic", "tests/frontend_tests.c:method_syntax", "tests/frontend_tests.c:range_for_syntax", "tests/fixtures/codegen_associated_types.r", "examples/generics/modules.map", "examples/methods/modules.map", "examples/reflection/modules.map", "examples/unzip/modules.map"],
        },
        "A.5": {
            "implementation": ["compiler/parser/parser.c:statements", "compiler/parser/static_conditions.inc"],
            "tests": ["tests/frontend_tests.c:restricted_syntax", "tests/frontend_tests.c:static_conditions", "tests/fixtures/codegen_static_conditions.r", "tests/fixtures/codegen_async_static_conditions.r", "tests/frontend_tests.c:await_calls", "tests/frontend_tests.c:lambda_syntax", "tests/frontend_tests.c:lambda_recovery", "tests/frontend_tests.c:range_for_syntax", "tests/frontend_tests.c:range_for_recovery", "tests/fixtures/codegen_await_calls.r", "tests/fixtures/codegen_closures.r", "tests/fixtures/codegen_range_for.r", "tests/fixtures/codegen_scoped_select.r", "tests/fixtures/codegen_deadline_blocks.r", "tests/fixtures/codegen_expression_forms.r", "examples/netlab/modules.map", "examples/unzip/modules.map"],
        },
        "A.6": {
            "implementation": ["compiler/parser/parser.c:expressions"],
            "tests": ["tests/frontend_tests.c:restricted_syntax", "tests/frontend_tests.c:method_syntax", "tests/frontend_tests.c:range_for_syntax", "tests/frontend_tests.c:collection_syntax", "tests/frontend_tests.c:collection_recovery", "tests/frontend_tests.c:reflection_syntax", "tests/frontend_tests.c:reflection_recovery", "tests/fixtures/codegen_membership.r", "tests/fixtures/codegen_collections.r", "tests/fixtures/codegen_variadic.r", "tests/fixtures/codegen_reflection.r", "examples/methods/modules.map", "examples/reflection/modules.map", "examples/unzip/modules.map"],
        },
    }
    sections: dict[str, object] = {}
    for section, contract in coverage_contracts.items():
        names = [item["name"] for item in productions if item["section"] == section]
        sections[section] = {
            "production_count": len(names),
            "production_names_sha256": hashlib.sha256(
                ("\n".join(names) + "\n").encode("utf-8")
            ).hexdigest(),
            **contract,
        }
    return {
        "schema_version": 1,
        "core_revision": "0.1.0-draft.105",
        "annex": "A",
        "annex_sha256": digest,
        "production_count": len(productions),
        "sections": sections,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--specification", required=True, type=Path)
    parser.add_argument("--manifest", type=Path)
    output_mode = parser.add_mutually_exclusive_group()
    output_mode.add_argument("--print", action="store_true", dest="print_manifest")
    output_mode.add_argument("--write", action="store_true", dest="write_manifest")
    arguments = parser.parse_args()

    try:
        expected = generated_manifest(arguments.specification)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"grammar check: {error}", file=sys.stderr)
        return 2
    if arguments.print_manifest:
        print(json.dumps(expected, indent=2, ensure_ascii=False) + "\n", end="")
        return 0
    if arguments.manifest is None:
        parser.error("--manifest is required unless --print is used")
    if arguments.write_manifest:
        try:
            arguments.manifest.parent.mkdir(parents=True, exist_ok=True)
            arguments.manifest.write_text(
                json.dumps(expected, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
        except OSError as error:
            print(f"grammar check: {error}", file=sys.stderr)
            return 2
        print(
            f"grammar manifest written: {expected['production_count']} productions, "
            f"sha256={expected['annex_sha256']}"
        )
        return 0
    try:
        actual = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"grammar check: {error}", file=sys.stderr)
        return 2
    if actual != expected:
        print(
            "grammar manifest is stale; review parser/tests and regenerate with --print",
            file=sys.stderr,
        )
        return 1
    print(
        f"grammar manifest: {expected['production_count']} productions, "
        f"sha256={expected['annex_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
