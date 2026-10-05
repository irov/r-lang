#!/usr/bin/env python3
"""Generate or verify machine-readable normative R rule catalogs."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


DOCUMENT_CONTRACTS = {
    "R_LANGUAGE_SPECIFICATION_0_1.en.adoc": ("0.1.0-draft.97", 496),
    "R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc": ("0.1.0-draft.67", 476),
}

REVISION_RE = re.compile(r"^\|Document revision\|([^\s]+)$", re.MULTILINE)
LANGUAGE_RE = re.compile(r"^:lang:\s*(\S+)\s*$", re.MULTILINE)
HEADING_RE = re.compile(r"^(=+) (.+)$")
ANCHOR_RE = re.compile(r"^\[\[([^\]]+)\]\]$")
RULE_RE = re.compile(r"^\*(R-[A-Z0-9-]+)\*\s+—")


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False) + "\n"


def display_path(path: Path) -> str:
    if path.parent.name == "specification":
        return f"specification/{path.name}"
    return path.as_posix()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def build_inventory(specification: Path) -> dict[str, Any]:
    source_bytes = specification.read_bytes()
    source = source_bytes.decode("utf-8")
    lines = source.splitlines()

    language = LANGUAGE_RE.findall(source)
    if language != ["en"]:
        raise ValueError("normative rule inventory requires one :lang: en declaration")
    revisions = REVISION_RE.findall(source)
    if len(revisions) != 1:
        raise ValueError("specification must contain exactly one document revision")

    contract = DOCUMENT_CONTRACTS.get(specification.name)
    if contract is not None and revisions[0] != contract[0]:
        raise ValueError(
            f"{specification.name} revision is {revisions[0]}, expected {contract[0]}"
        )

    heading_stack: list[dict[str, Any]] = []
    anchors: dict[str, int] = {}
    rule_positions: list[tuple[str, int, int, list[dict[str, Any]]]] = []

    for index, line in enumerate(lines):
        line_number = index + 1
        heading_match = HEADING_RE.fullmatch(line)
        if heading_match is not None:
            level = len(heading_match.group(1))
            heading = {
                "level": level,
                "title": heading_match.group(2),
                "line": line_number,
            }
            while heading_stack and heading_stack[-1]["level"] >= level:
                heading_stack.pop()
            heading_stack.append(heading)
            continue

        anchor_match = ANCHOR_RE.fullmatch(line)
        if anchor_match is not None:
            anchor = anchor_match.group(1)
            if anchor in anchors:
                raise ValueError(f"duplicate anchor {anchor}")
            anchors[anchor] = line_number
            continue

        rule_match = RULE_RE.match(line)
        if rule_match is None:
            continue
        rule_id = rule_match.group(1)
        previous = index - 1
        while previous >= 0 and not lines[previous].strip():
            previous -= 1
        if previous < 0 or lines[previous] != f"[[{rule_id}]]":
            raise ValueError(f"rule {rule_id} is not immediately owned by its anchor")
        rule_positions.append(
            (rule_id, anchors[rule_id], line_number, [dict(value) for value in heading_stack])
        )

    ids = [record[0] for record in rule_positions]
    duplicates = sorted(rule_id for rule_id, count in Counter(ids).items() if count > 1)
    if duplicates:
        raise ValueError("duplicate normative rule IDs: " + ", ".join(duplicates))
    if contract is not None and len(rule_positions) != contract[1]:
        raise ValueError(
            f"{specification.name} has {len(rule_positions)} rules, expected {contract[1]}"
        )

    anchor_line_numbers = sorted(anchors.values())
    rules: list[dict[str, Any]] = []
    for rule_id, anchor_line, declaration_line, rule_headings in rule_positions:
        later_anchors = [line for line in anchor_line_numbers if line > anchor_line]
        next_anchor_line = later_anchors[0] if later_anchors else len(lines) + 1
        end_line = next_anchor_line - 1
        while end_line >= declaration_line and not lines[end_line - 1].strip():
            end_line -= 1
        normalized_text = "\n".join(lines[declaration_line - 1:end_line]) + "\n"
        rules.append(
            {
                "id": rule_id,
                "anchor": rule_id,
                "headings": rule_headings,
                "source_location": {
                    "path": display_path(specification),
                    "anchor_line": anchor_line,
                    "declaration_line": declaration_line,
                    "declaration_column": 1,
                    "end_line": end_line,
                },
                "text_sha256": sha256_bytes(normalized_text.encode("utf-8")),
            }
        )

    rules_digest = sha256_bytes(
        json.dumps(rules, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
    )
    return {
        "schema_version": 1,
        "catalog_kind": "normative_rule_catalog",
        "implementation_coverage": None,
        "coverage_note": (
            "This catalog records normative source rules only. It makes no claim that a rule "
            "is implemented, tested or otherwise covered."
        ),
        "generated_from": {
            "path": display_path(specification),
            "revision": revisions[0],
            "sha256": sha256_bytes(source_bytes),
        },
        "rule_count": len(rules),
        "rules_sha256": rules_digest,
        "rules": rules,
    }


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("inventory root must be an object")
    return value


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--specification", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--verify", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        expected = build_inventory(arguments.specification)
        expected_text = canonical_json(expected)
        if arguments.write:
            arguments.inventory.parent.mkdir(parents=True, exist_ok=True)
            arguments.inventory.write_text(expected_text, encoding="utf-8", newline="\n")
            print(
                f"wrote {expected['rule_count']} normative rules to {arguments.inventory}"
            )
            return 0
        if not arguments.inventory.is_file():
            print(f"missing rule inventory: {arguments.inventory}", file=sys.stderr)
            return 1
        actual = load_json(arguments.inventory)
        actual_text = arguments.inventory.read_text(encoding="utf-8")
        if actual_text != expected_text or canonical_json(actual) != expected_text:
            print(
                "normative rule inventory is stale; regenerate it with --write",
                file=sys.stderr,
            )
            return 1
        print(
            f"rule inventory valid: {expected['rule_count']} rules, "
            f"sha256={expected['rules_sha256']}"
        )
        return 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"rule inventory generation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
