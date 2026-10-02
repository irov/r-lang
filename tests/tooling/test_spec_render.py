#!/usr/bin/env python3
"""Regression tests for the self-contained pinned Asciidoctor environment."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

import check_spec_render  # noqa: E402


class SpecificationRenderTests(unittest.TestCase):
    def test_colocated_gem_tree_overrides_ambient_gem_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            gem_home = Path(temporary_directory) / "asciidoctor-2.0.26"
            executable = gem_home / "bin/asciidoctor"
            gemspec = gem_home / "specifications/asciidoctor-2.0.26.gemspec"
            executable.parent.mkdir(parents=True)
            gemspec.parent.mkdir(parents=True)
            executable.touch()
            gemspec.touch()

            with mock.patch.dict(
                os.environ, {"GEM_HOME": "/ambient/home", "GEM_PATH": "/ambient/path"}
            ):
                environment = check_spec_render.asciidoctor_environment(
                    str(executable), "2.0.26"
                )

            self.assertEqual(environment["GEM_HOME"], str(gem_home.resolve()))
            self.assertEqual(environment["GEM_PATH"], str(gem_home.resolve()))

    def test_external_executable_preserves_ambient_gem_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            executable = Path(temporary_directory) / "bin/asciidoctor"
            executable.parent.mkdir(parents=True)
            executable.touch()

            with mock.patch.dict(
                os.environ, {"GEM_HOME": "/ambient/home", "GEM_PATH": "/ambient/path"}
            ):
                environment = check_spec_render.asciidoctor_environment(
                    str(executable), "2.0.26"
                )

            self.assertEqual(environment["GEM_HOME"], "/ambient/home")
            self.assertEqual(environment["GEM_PATH"], "/ambient/path")


if __name__ == "__main__":
    unittest.main()
