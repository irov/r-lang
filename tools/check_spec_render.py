#!/usr/bin/env python3
"""Render all normative R specifications with the pinned Asciidoctor."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


VERSION_PATTERN = re.compile(r"\b(\d+\.\d+\.\d+)\b")
SPECIFICATIONS = (
    "R_LANGUAGE_SPECIFICATION_0_1.en.adoc",
    "R_LANGUAGE_SPECIFICATION_0_1.ru.adoc",
    "R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc",
    "R_STANDARD_LIBRARY_SPECIFICATION_0_1.ru.adoc",
)


def pinned_version(root: Path) -> str:
    for line in (root / "tools/toolchain.lock").read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator and key.strip() == "asciidoctor":
            return value.strip()
    raise ValueError("tools/toolchain.lock has no asciidoctor entry")


def asciidoctor_environment(executable: str, expected_version: str) -> dict[str, str]:
    """Use a colocated Ruby gem tree without relying on the caller's GEM_HOME."""

    environment = os.environ.copy()
    executable_path = Path(executable)
    resolved_executable: Path | None = None
    if executable_path.parent != Path(".") or executable_path.is_absolute():
        resolved_executable = executable_path.resolve()
    else:
        located = shutil.which(executable)
        if located is not None:
            resolved_executable = Path(located).resolve()
    if resolved_executable is None:
        return environment

    gem_home = resolved_executable.parent.parent
    gemspec = gem_home / "specifications" / f"asciidoctor-{expected_version}.gemspec"
    if gemspec.is_file():
        environment["GEM_HOME"] = str(gem_home)
        environment["GEM_PATH"] = str(gem_home)
    return environment


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--asciidoctor", default="asciidoctor")
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    try:
        expected_version = pinned_version(root)
        environment = asciidoctor_environment(arguments.asciidoctor, expected_version)
        version = subprocess.run(
            [arguments.asciidoctor, "--version"],
            check=False,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if version.returncode != 0:
            raise RuntimeError(version.stdout.strip() or "version command failed")
        match = VERSION_PATTERN.search(version.stdout)
        if match is None:
            raise RuntimeError(f"cannot parse Asciidoctor version: {version.stdout.strip()}")
        if match.group(1) != expected_version:
            print(
                f"spec render error: expected Asciidoctor {expected_version}, "
                f"found {match.group(1)}",
                file=sys.stderr,
            )
            return 2
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory)
            command = [
                arguments.asciidoctor,
                "--failure-level",
                "WARN",
                "-D",
                str(output),
            ]
            command.extend(str(root / "specification" / name) for name in SPECIFICATIONS)
            rendered = subprocess.run(command, check=False, env=environment)
            if rendered.returncode != 0:
                return 1
            for name in SPECIFICATIONS:
                html = output / f"{Path(name).stem}.html"
                if not html.is_file() or html.stat().st_size == 0:
                    print(f"spec render error: missing output {html.name}", file=sys.stderr)
                    return 1
        print(
            f"spec render valid: {len(SPECIFICATIONS)} documents with "
            f"Asciidoctor {expected_version}"
        )
        return 0
    except (OSError, UnicodeError, ValueError, RuntimeError) as error:
        print(f"spec render failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
