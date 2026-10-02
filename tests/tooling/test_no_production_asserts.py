#!/usr/bin/env python3
"""Regression tests for the production assertion gate."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPOSITORY_ROOT / "tools/check_no_production_asserts.py"


def run_checker(root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


class NoProductionAssertionsTests(unittest.TestCase):
    def write_source(self, root: Path, relative: str, text: str) -> None:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def test_direct_code_and_static_assert_are_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_source(
                root,
                "runtime/source/example.c",
                "_Static_assert(sizeof(unsigned) >= 2, \"ABI\");\n"
                "unsigned increment(unsigned value) { return value + 1U; }\n",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_internal_assertion_token_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            forbidden = "R_" + "INTERNAL_ASSERT"
            self.write_source(
                root,
                "library/std/example/source/run.c",
                f"void run(int value) {{ {forbidden}(value != 0); }}\n",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("forbidden production assertion token", result.stderr)

    def test_emitted_assertion_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_source(
                root,
                "compiler/codegen/example.c",
                'const char *line = "assert(value != 0);";\n',
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("forbidden C assert call", result.stderr)

    def test_assert_header_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_source(
                root,
                "runtime/source/example.c",
                "#include <assert.h>\n",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("forbidden C assert header", result.stderr)


if __name__ == "__main__":
    unittest.main()
