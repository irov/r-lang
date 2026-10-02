#!/usr/bin/env python3
"""Check C-style invariants that clang-format cannot express."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SOURCE_ROOTS = ("compiler", "runtime", "library", "tests")
GENERATED_FILES = {
    "compiler/lexer/lexer_generated.c",
    "compiler/source/unicode_data.c",
    "compiler/source/unicode_data.h",
    "runtime/freestanding/include/r_runtime_target_abi.h",
    "runtime/include/r_runtime_target_abi.h",
}
GENERATED_PREFIXES = ("library/generated/",)
ALLOCATION_BOUNDARIES = {
    "compiler/cli/allocation.c",
    "compiler/source/frontend.c",
    "runtime/source/allocator.c",
}
DIRECT_ALLOCATION_PATTERN = re.compile(
    r"(?<![.>])\b(malloc|calloc|realloc|free)\s*\("
)
GOTO_PATTERN = re.compile(r"\bgoto\s+([A-Za-z_][A-Za-z0-9_]*)\s*;")
LABEL_PATTERN = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(?://.*)?$")


def collect_sources(root: Path) -> list[Path]:
    result: list[Path] = []
    for source_root in SOURCE_ROOTS:
        directory = root / source_root
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix not in {".c", ".h"}:
                continue
            relative = path.relative_to(root).as_posix()
            if relative in GENERATED_FILES:
                continue
            if any(relative.startswith(prefix) for prefix in GENERATED_PREFIXES):
                continue
            result.append(path)
    return sorted(result)


def strip_comments_and_literals(text: str) -> str:
    """Preserve newlines while removing tokens that may contain rule-like text."""

    result: list[str] = []
    index = 0
    state = "code"
    while index < len(text):
        character = text[index]
        next_character = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if character == "/" and next_character == "*":
                result.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            if character == "/" and next_character == "/":
                result.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            if character == '"':
                result.append(" ")
                index += 1
                state = "string"
                continue
            if character == "'":
                result.append(" ")
                index += 1
                state = "character"
                continue
            result.append(character)
            index += 1
            continue
        if state == "line_comment":
            if character == "\n":
                result.append("\n")
                state = "code"
            else:
                result.append(" ")
            index += 1
            continue
        if state == "block_comment":
            if character == "*" and next_character == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
            else:
                result.append("\n" if character == "\n" else " ")
                index += 1
            continue
        if character == "\\" and next_character:
            result.append(" ")
            result.append("\n" if next_character == "\n" else " ")
            index += 2
            continue
        if (state == "string" and character == '"') or (
            state == "character" and character == "'"
        ):
            result.append(" ")
            index += 1
            state = "code"
            continue
        result.append("\n" if character == "\n" else " ")
        index += 1
    return "".join(result)


def check_source(root: Path, path: Path) -> list[str]:
    relative = path.relative_to(root).as_posix()
    text = strip_comments_and_literals(path.read_text(encoding="utf-8"))
    errors: list[str] = []

    if relative not in ALLOCATION_BOUNDARIES and not relative.startswith("tests/"):
        for match in DIRECT_ALLOCATION_PATTERN.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            errors.append(
                f"{relative}:{line}: direct {match.group(1)} is outside allocator subsystem"
            )
    for match in GOTO_PATTERN.finditer(text):
        if match.group(1) != "cleanup":
            line = text.count("\n", 0, match.start()) + 1
            errors.append(
                f"{relative}:{line}: goto target must be the final cleanup label"
            )
    for line_number, line_text in enumerate(text.splitlines(), start=1):
        match = LABEL_PATTERN.match(line_text)
        if match is not None and match.group(1) not in {"case", "default", "cleanup"}:
            errors.append(
                f"{relative}:{line_number}: handwritten label must be named cleanup"
            )
    return errors


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    root = parse_arguments().root.resolve()
    try:
        sources = collect_sources(root)
        errors = [error for path in sources for error in check_source(root, path)]
        if errors:
            for error in errors:
                print(f"C style error: {error}", file=sys.stderr)
            return 1
        print(f"C style valid: {len(sources)} handwritten C files")
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print(f"C style check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
