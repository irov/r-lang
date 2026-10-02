#!/usr/bin/env python3

import argparse
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--depth", type=int, default=32000)
    parser.add_argument("--timeout", type=float, default=3.0)
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
        try:
            result = subprocess.run(
                [arguments.compiler, "--emit=ast", str(source_path)],
                capture_output=True,
                text=True,
                timeout=arguments.timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(
                f"parser did not enforce the nesting limit within {arguments.timeout:.1f}s "
                f"for depth {arguments.depth}"
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
