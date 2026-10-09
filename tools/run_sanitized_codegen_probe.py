#!/usr/bin/env python3
"""Compile and execute one R fixture through a configured sanitizer CTest driver."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


TEMPLATE_TEST = "r_frontend_codegen_audit_call_borrow_then_move_own_distinct_origins"


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=root / "build-sanitize")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--expect-diagnostic")
    args = parser.parse_args()

    build = args.build.resolve()
    source = args.source.resolve()
    output_directory = build / "tests"
    if not source.is_file():
        parser.error(f"source does not exist: {source}")

    listed = subprocess.run(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        check=False,
        capture_output=True,
        text=True,
    )
    if listed.returncode != 0:
        sys.stderr.write(listed.stderr)
        return listed.returncode
    try:
        tests = json.loads(listed.stdout)["tests"]
        template = next(test for test in tests if test["name"] == TEMPLATE_TEST)
    except (json.JSONDecodeError, KeyError, StopIteration) as error:
        print(f"could not load sanitizer CTest template: {error}", file=sys.stderr)
        return 2

    output_directory.mkdir(parents=True, exist_ok=True)
    output = output_directory / source.stem
    replacements = {
        "-DSOURCE_1=": f"-DSOURCE_1={source}",
        "-DOUTPUT_EXE=": f"-DOUTPUT_EXE={output}",
    }
    command = list(template["command"])
    for index, argument in enumerate(command):
        for prefix, replacement in replacements.items():
            if argument.startswith(prefix):
                command[index] = replacement
                break

    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "abort_on_error=1:symbolize=0"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    completed = subprocess.run(
        command,
        cwd=build / "tests",
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    if args.expect_diagnostic is not None:
        diagnostics = completed.stdout + completed.stderr
        if (completed.returncode != 0) and (args.expect_diagnostic in diagnostics):
            return 0
        print(
            f"expected rejected input with diagnostic {args.expect_diagnostic}",
            file=sys.stderr,
        )
        return 1
    if Path(f"{output}.o").is_file():
        print(f"program object: {output}.o", file=sys.stderr)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
