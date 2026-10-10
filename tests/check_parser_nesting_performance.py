#!/usr/bin/env python3
"""The parser rejects nesting beyond its limit (R-DIAG-LIMIT-001) in bounded work. The bound is on
the processor time of the compiler, not on the wall clock: under a loaded machine or a sanitizer
the wall clock grows with the waiting for a processor while the work stays the same (B10-4). A
generous wall-clock timeout still catches a hang."""

import argparse
import resource
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--depth", type=int, default=32000)
    parser.add_argument("--timeout", type=float, default=3.0,
                        help="processor seconds (user and system) the compiler may use")
    parser.add_argument("--wall-timeout", type=float, default=120.0)
    arguments = parser.parse_args()

    source = (
        "module test.regression.parser_nesting_performance;\n"
        "i32 main() { return "
        + "(" * arguments.depth
        + "0"
        + ")" * arguments.depth
        + "; }\n"
    )
    with tempfile.TemporaryDirectory(prefix="r-parser-nesting-") as directory:
        source_path = Path(directory) / "nested.r"
        source_path.write_text(source, encoding="utf-8")
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        try:
            result = subprocess.run(
                [arguments.compiler, "--emit=ast", str(source_path)],
                capture_output=True,
                text=True,
                timeout=arguments.wall_timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(
                f"parser did not finish within {arguments.wall_timeout:.1f}s of wall-clock "
                f"time for depth {arguments.depth}"
            )
            return 1
        after = resource.getrusage(resource.RUSAGE_CHILDREN)

    used = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    if used > arguments.timeout:
        print(
            f"parser did not enforce the nesting limit within {arguments.timeout:.1f}s of "
            f"processor time for depth {arguments.depth} ({used:.2f}s)"
        )
        return 1
    if result.returncode == 0:
        print("over-nested source was accepted")
        return 1
    if "R-DIAG-LIMIT-001" not in result.stderr:
        print("over-nested source did not report R-DIAG-LIMIT-001")
        print(result.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
