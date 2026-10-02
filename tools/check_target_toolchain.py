#!/usr/bin/env python3
"""Verify the compiler and SDK used for a pinned R target manifest."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def run(command: list[str]) -> str:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"could not execute {' '.join(command)}: {error}") from error
    require(
        completed.returncode == 0,
        f"{' '.join(command)} failed ({completed.returncode}): "
        f"{completed.stdout}{completed.stderr}",
    )
    return completed.stdout.strip()


def require_object(value: Any, name: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{name} must be an object")
    return value


def check(arguments: argparse.Namespace) -> None:
    manifest = require_object(
        json.loads(arguments.manifest.read_text(encoding="utf-8")),
        "target manifest",
    )
    identity = require_object(manifest.get("identity"), "target manifest identity")
    toolchain = require_object(manifest.get("toolchain"), "target manifest toolchain")

    compiler_name = toolchain.get("c_compiler")
    compiler_version = toolchain.get("c_compiler_version")
    compiler_build = toolchain.get("c_compiler_build")
    target_triple = identity.get("target_triple")
    sdk = toolchain.get("sdk")
    for value, name in (
        (compiler_name, "C compiler"),
        (compiler_version, "C compiler version"),
        (compiler_build, "C compiler build"),
        (target_triple, "target triple"),
        (sdk, "SDK"),
    ):
        require(isinstance(value, str) and value, f"target manifest {name} is invalid")

    version_output = run([arguments.cc, "--version"])
    version_line = version_output.splitlines()[0] if version_output else ""
    expected_version_line = f"{compiler_name} version {compiler_version} ({compiler_build})"
    require(
        version_line == expected_version_line,
        f"C compiler identity mismatch: expected '{expected_version_line}', got '{version_line}'",
    )

    actual_triple = run([arguments.cc, "-dumpmachine"])
    require(
        re.fullmatch(re.escape(target_triple) + r"(?:[0-9].*)?", actual_triple) is not None,
        f"C compiler target mismatch: expected '{target_triple}', got '{actual_triple}'",
    )

    require(sdk.startswith("macOS "), "target manifest SDK must use 'macOS VERSION'")
    expected_sdk_version = sdk.removeprefix("macOS ")
    actual_sdk_version = run([arguments.xcrun, "--sdk", "macosx", "--show-sdk-version"])
    require(
        actual_sdk_version == expected_sdk_version,
        f"SDK version mismatch: expected '{expected_sdk_version}', got '{actual_sdk_version}'",
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--xcrun", default="xcrun")
    return parser.parse_args()


def main() -> int:
    try:
        check(parse_arguments())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"target toolchain check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
