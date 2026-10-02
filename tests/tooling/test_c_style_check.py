#!/usr/bin/env python3
"""Regression tests for non-format C-style gates."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPOSITORY_ROOT / "tools/check_c_style.py"


def run_checker(root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


class CStyleCheckTests(unittest.TestCase):
    def test_cleanup_and_allocator_boundary_are_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "compiler").mkdir()
            (root / "runtime/source").mkdir(parents=True)
            (root / "compiler/example.c").write_text(
                "void example(void) {\n"
                "    goto cleanup;\n"
                "cleanup:\n"
                "    return;\n"
                "}\n",
                encoding="utf-8",
            )
            (root / "runtime/source/allocator.c").write_text(
                "void *allocate(unsigned long size) { return malloc(size); }\n",
                encoding="utf-8",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_non_cleanup_goto_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "compiler").mkdir()
            (root / "compiler/example.c").write_text(
                "void example(void) { goto retry; retry: return; }\n",
                encoding="utf-8",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("goto target", result.stderr)

    def test_direct_allocation_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "library").mkdir()
            (root / "library/example.c").write_text(
                "void *example(unsigned long size) { return calloc(1, size); }\n",
                encoding="utf-8",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("direct calloc", result.stderr)

    def test_generated_lexer_is_excluded(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "compiler/lexer").mkdir(parents=True)
            (root / "compiler/lexer/lexer_generated.c").write_text(
                "void generated(void) { goto yy1; yy1: return; }\n",
                encoding="utf-8",
            )

            result = run_checker(root)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
