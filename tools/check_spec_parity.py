#!/usr/bin/env python3
"""Validate bilingual R Core and Standard Library specification parity."""

from __future__ import annotations

import argparse
import collections
import re
import sys
from pathlib import Path


DOCUMENT_STEMS = (
    "R_LANGUAGE_SPECIFICATION_0_1",
    "R_STANDARD_LIBRARY_SPECIFICATION_0_1",
)

ANCHOR_RE = re.compile(r"^\[\[([^\]]+)\]\]$", re.MULTILINE)
HEADING_RE = re.compile(r"^(=+) .+$", re.MULTILINE)
REVISION_RE = re.compile(r"^\|(?:Document revision|Редакция документа)\s*\|([^\s]+)$", re.MULTILINE)
RULE_DECL_RE = re.compile(r"^\*(R-[A-Z0-9-]+)\*\s+—", re.MULTILINE)
RULE_TOKEN_RE = re.compile(r"\bR-[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+\b")
CATALOG_ID_RE = re.compile(r"^\|`\+(R-[A-Z0-9-]+)\+`", re.MULTILINE)
INLINE_CODE_RE = re.compile(r"`\+(.*?)\+`", re.DOTALL)
XREF_RE = re.compile(r"xref:([^\[#]+)(?:#[^\[]+)?\[")


class ParityError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ParityError(message)


def unique_anchors(text: str, label: str) -> list[str]:
    anchors = ANCHOR_RE.findall(text)
    duplicates = sorted(name for name, count in collections.Counter(anchors).items() if count > 1)
    require(not duplicates, f"{label}: duplicate anchors: {', '.join(duplicates)}")
    return anchors


def source_blocks(text: str, label: str) -> list[tuple[str, str]]:
    lines = text.splitlines()
    result: list[tuple[str, str]] = []
    index = 0
    while index < len(lines):
        match = re.fullmatch(r"\[source(?:,([^\]]+))?\]", lines[index])
        if match is None:
            index += 1
            continue
        require(index + 1 < len(lines), f"{label}:{index + 1}: source block has no delimiter")
        delimiter = lines[index + 1]
        require(delimiter in ("----", "...."),
                f"{label}:{index + 2}: unsupported source delimiter {delimiter!r}")
        end = index + 2
        while end < len(lines) and lines[end] != delimiter:
            end += 1
        require(end < len(lines), f"{label}:{index + 1}: unterminated source block")
        result.append((match.group(1) or "", "\n".join(lines[index + 2:end])))
        index = end + 1
    return result


def table_shapes(text: str, label: str) -> list[tuple[int, ...]]:
    lines = text.splitlines()
    result: list[tuple[int, ...]] = []
    index = 0
    while index < len(lines):
        if lines[index].strip() != "|===":
            index += 1
            continue
        shape: list[int] = []
        index += 1
        while index < len(lines) and lines[index].strip() != "|===":
            line = lines[index]
            shape.append(line.count("|") if line.startswith("|") else 0)
            index += 1
        require(index < len(lines), f"{label}: unterminated table")
        result.append(tuple(shape))
        index += 1
    return result


def api_signatures(text: str) -> collections.Counter[str]:
    signatures: list[str] = []
    for value in INLINE_CODE_RE.findall(text):
        function_like = "(" in value and ")" in value
        qualified_or_typed = "::" in value or value.startswith(("async ", "raw fn", "unsafe "))
        if "->" in value or (function_like and qualified_or_typed):
            signatures.append(value)
    return collections.Counter(signatures)


def check_pair(stem: str, en_path: Path, ru_path: Path) -> tuple[str, str]:
    en_text = en_path.read_text(encoding="utf-8")
    ru_text = ru_path.read_text(encoding="utf-8")

    en_revision = REVISION_RE.findall(en_text)
    ru_revision = REVISION_RE.findall(ru_text)
    require(len(en_revision) == 1, f"{en_path}: expected exactly one document revision")
    require(len(ru_revision) == 1, f"{ru_path}: expected exactly one document revision")
    require(en_revision == ru_revision,
            f"{stem}: revision mismatch: EN {en_revision[0]}, RU {ru_revision[0]}")

    en_anchors = unique_anchors(en_text, str(en_path))
    ru_anchors = unique_anchors(ru_text, str(ru_path))
    require(en_anchors == ru_anchors, f"{stem}: anchor sequence differs")

    en_rules = RULE_DECL_RE.findall(en_text)
    ru_rules = RULE_DECL_RE.findall(ru_text)
    require(collections.Counter(en_rules) == collections.Counter(ru_rules),
            f"{stem}: normative rule-ID multiset differs")
    require(en_rules == ru_rules, f"{stem}: normative rule-ID sequence differs")
    for rule in en_rules:
        require(rule in en_anchors, f"{en_path}: rule {rule} has no matching anchor")

    en_heading_roles = [len(markers) for markers in HEADING_RE.findall(en_text)]
    ru_heading_roles = [len(markers) for markers in HEADING_RE.findall(ru_text)]
    require(en_heading_roles == ru_heading_roles, f"{stem}: heading structural roles differ")

    require(table_shapes(en_text, str(en_path)) == table_shapes(ru_text, str(ru_path)),
            f"{stem}: table structure differs")
    require(source_blocks(en_text, str(en_path)) == source_blocks(ru_text, str(ru_path)),
            f"{stem}: source/EBNF blocks differ")
    require(api_signatures(en_text) == api_signatures(ru_text),
            f"{stem}: API signature blocks differ")

    return en_text, ru_text


def check_references(spec_dir: Path, documents: list[tuple[Path, str]]) -> None:
    combined = "\n".join(text for _, text in documents)
    definitions = set(ANCHOR_RE.findall(combined))
    definitions.update(CATALOG_ID_RE.findall(combined))
    for path, text in documents:
        unresolved = sorted(set(RULE_TOKEN_RE.findall(text)) - definitions)
        require(not unresolved, f"{path}: unresolved R references: {', '.join(unresolved)}")
        for target in XREF_RE.findall(text):
            require((path.parent / target).is_file(), f"{path}: unresolved xref target {target}")


def parse_arguments() -> argparse.Namespace:
    default_spec_dir = Path(__file__).resolve().parent.parent / "specification"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec-dir", type=Path, default=default_spec_dir)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    spec_dir = arguments.spec_dir.resolve()
    documents: list[tuple[Path, str]] = []
    try:
        for stem in DOCUMENT_STEMS:
            en_path = spec_dir / f"{stem}.en.adoc"
            ru_path = spec_dir / f"{stem}.ru.adoc"
            require(en_path.is_file(), f"missing specification: {en_path}")
            require(ru_path.is_file(), f"missing specification: {ru_path}")
            en_text, ru_text = check_pair(stem, en_path, ru_path)
            documents.extend(((en_path, en_text), (ru_path, ru_text)))
            print(f"PARITY OK {stem}")
        check_references(spec_dir, documents)
        print("REFERENCES OK")
    except (OSError, UnicodeError, ParityError) as error:
        print(f"SPEC PARITY ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
