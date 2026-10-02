#!/usr/bin/env python3
"""Regression tests for the pinned non-mutating C format gate."""

from __future__ import annotations

import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FORMAT_CHECKER = REPOSITORY_ROOT / "tools/check_format.py"


def write_formatter(path: Path, version: str) -> None:
    path.write_text(
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--version\" ]; then\n"
        f"    printf 'clang-format version {version}\\n'\n"
        "    exit 0\n"
        "fi\n"
        "exit 0\n",
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def make_fixture(root: Path, source: bytes = b"int value;\n") -> Path:
    (root / "tools").mkdir()
    (root / "compiler").mkdir()
    (root / "tools/toolchain.lock").write_text("clang-format=22.1.8\n", encoding="utf-8")
    (root / ".clang-format").write_text("BasedOnStyle: LLVM\n", encoding="utf-8")
    source_path = root / "compiler/example.c"
    source_path.write_bytes(source)
    return source_path


def run_checker(root: Path, formatter: Path) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    return subprocess.run(
        [
            sys.executable,
            str(FORMAT_CHECKER),
            "--root",
            str(root),
            "--clang-format",
            str(formatter),
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )


class FormatCheckTests(unittest.TestCase):
    def test_exact_formatter_version_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            make_fixture(root)
            formatter = root / "clang-format"
            write_formatter(formatter, "22.1.8")

            result = run_checker(root, formatter)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("clang-format 22.1.8", result.stdout)

    def test_wrong_formatter_version_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            make_fixture(root)
            formatter = root / "clang-format"
            write_formatter(formatter, "21.0.0")

            result = run_checker(root, formatter)
            self.assertEqual(result.returncode, 2)
            self.assertIn("expected clang-format 22.1.8", result.stderr)

    def test_text_violation_precedes_formatter_execution(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            make_fixture(root, b"int value; \n")
            formatter = root / "missing-clang-format"

            result = run_checker(root, formatter)
            self.assertEqual(result.returncode, 1)
            self.assertIn("trailing whitespace", result.stderr)
            self.assertNotIn("format check failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
