#!/usr/bin/env python3
"""Reject runtime assertions from production and generated C sources."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


PRODUCTION_ROOTS = ("compiler", "runtime", "library")
GENERATED_FIXTURE_ROOTS = ("tests/golden",)
SOURCE_SUFFIXES = {".c", ".h"}
FORBIDDEN_TEXT = (
    "R_INTERNAL_ASSERT",
    "r_runtime_assert.h",
    "R_RUNTIME_ASSERTIONS_ENABLED",
    "R_RUNTIME_ENABLE_ASSERTIONS",
    "r_runtime_assertion_failed",
)
ASSERT_INCLUDE_PATTERN = re.compile(r"#\s*include\s*[<\"]assert\.h[>\"]")
ASSERT_CALL_PATTERN = re.compile(r"(?<![A-Za-z0-9_])assert\s*\(")


def collect_sources(root: Path) -> list[Path]:
    sources: list[Path] = []
    for relative_root in (*PRODUCTION_ROOTS, *GENERATED_FIXTURE_ROOTS):
        directory = root / relative_root
        if not directory.exists():
            continue
        sources.extend(
            path
            for path in directory.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )
    return sorted(set(sources))


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def check_source(root: Path, path: Path) -> list[str]:
    relative = path.relative_to(root).as_posix()
    text = path.read_text(encoding="utf-8")
    errors: list[str] = []
    for forbidden in FORBIDDEN_TEXT:
        offset = text.find(forbidden)
        if offset >= 0:
            errors.append(
                f"{relative}:{line_number(text, offset)}: forbidden production assertion "
                f"token {forbidden}"
            )
    for description, pattern in (
        ("C assert header", ASSERT_INCLUDE_PATTERN),
        ("C assert call", ASSERT_CALL_PATTERN),
    ):
        match = pattern.search(text)
        if match is not None:
            errors.append(
                f"{relative}:{line_number(text, match.start())}: forbidden {description}"
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
                print(f"Production assertion error: {error}", file=sys.stderr)
            return 1
        print(f"Production assertions absent: {len(sources)} C files")
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Production assertion check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
