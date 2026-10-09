#!/usr/bin/env python3
"""Check or apply the pinned canonical format to handwritten R C sources."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


SOURCE_ROOTS = ("compiler", "runtime", "library", "tests")
SOURCE_SUFFIXES = {".c", ".h"}
GENERATED_FILES = {
    "compiler/lexer/lexer_generated.c",
    "compiler/source/unicode_data.c",
    "compiler/source/unicode_data.h",
    "runtime/freestanding/include/r_runtime_target_abi.h",
    "runtime/include/r_runtime_target_abi.h",
    "runtime/llvm/inline_shims.generated.c",
}
GENERATED_PREFIXES = ("library/generated/",)
VERSION_PATTERN = re.compile(r"\b(\d+\.\d+\.\d+)\b")


def expected_version(root: Path) -> str:
    lock_path = root / "tools/toolchain.lock"
    for line in lock_path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator and key.strip() == "clang-format":
            return value.strip()
    raise ValueError("tools/toolchain.lock has no clang-format entry")


def collect_sources(root: Path) -> list[Path]:
    sources = []
    for source_root in SOURCE_ROOTS:
        directory = root / source_root
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(root).as_posix()
            if relative in GENERATED_FILES:
                continue
            if any(relative.startswith(prefix) for prefix in GENERATED_PREFIXES):
                continue
            sources.append(path)
    return sorted(sources)


def check_text_invariants(root: Path, sources: list[Path]) -> list[str]:
    errors = []
    for path in sources:
        relative = path.relative_to(root)
        data = path.read_bytes()
        if b"\r" in data:
            errors.append(f"{relative}: CR byte is not permitted")
        if b"\t" in data:
            errors.append(f"{relative}: tab byte is not permitted")
        if data and not data.endswith(b"\n"):
            errors.append(f"{relative}: missing final newline")
        for line_number, line in enumerate(data.splitlines(), start=1):
            if line.endswith((b" ", b"\t")):
                errors.append(f"{relative}:{line_number}: trailing whitespace")
            if line.startswith((b"<<<<<<<", b"=======", b">>>>>>>")):
                errors.append(f"{relative}:{line_number}: merge-conflict marker")
    return errors


def formatter_version(executable: str) -> str:
    process = subprocess.run(
        [executable, "--version"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if process.returncode != 0:
        raise RuntimeError(f"{executable} --version failed: {process.stdout.strip()}")
    match = VERSION_PATTERN.search(process.stdout)
    if match is None:
        raise RuntimeError(f"cannot parse formatter version: {process.stdout.strip()}")
    return match.group(1)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--fix", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    try:
        sources = collect_sources(root)
        errors = check_text_invariants(root, sources)
        if errors:
            for error in errors:
                print(f"format error: {error}", file=sys.stderr)
            return 1
        required = expected_version(root)
        actual = formatter_version(arguments.clang_format)
        if actual != required:
            print(
                f"format error: expected clang-format {required}, found {actual}",
                file=sys.stderr,
            )
            return 2
        if not sources:
            print("format valid: no handwritten C sources")
            return 0
        command = [arguments.clang_format]
        command.extend(["-i"] if arguments.fix else ["--dry-run", "--Werror"])
        command.extend(str(path) for path in sources)
        process = subprocess.run(command, check=False)
        if process.returncode != 0:
            return 1
        action = "formatted" if arguments.fix else "format valid"
        print(f"{action}: {len(sources)} handwritten C files with clang-format {actual}")
        return 0
    except (OSError, UnicodeError, ValueError, RuntimeError) as error:
        print(f"format check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
