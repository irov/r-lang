#!/usr/bin/env python3
"""Check that the fixtures of index and conversion proofs compile and carry their marks.

A fixture marks each line that holds one index place with `/* proven */` (the bounds check may
be left out) or `/* checked */` (the check must stay), and each line that holds one explicit
integer conversion with `/* proven conversion */` or `/* checked conversion */`.

The LLVM emitter does not prove these places yet: index proofs become facts for the optimizer
in stage B7, and the comparison of each mark with the checks of the generated code returns
with it. Until then this check lowers every function of each fixture to LLVM IR and requires
the marks that the comparison will read; the codegen tests of the same fixtures run them as
programs.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys

MARK = re.compile(r'/\* (proven|checked)( conversion)? \*/\s*$')


def compile_fixture(front, fixture):
    result = subprocess.run([front, '--emit=llvm-ir', '--all-functions', str(fixture)],
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise SystemExit(f'{fixture.name}: r-front failed\n{result.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('fixtures', nargs='+', type=Path)
    args = parser.parse_args()
    marks = 0
    for fixture in args.fixtures:
        compile_fixture(args.front, fixture)
        marks += sum(1 for line in fixture.read_text().splitlines() if MARK.search(line))
    if marks == 0:
        print('no marked lines', file=sys.stderr)
        return 1
    print(f'index proofs: {marks} marked index places and conversions compile '
          '(their checks against the generated code return in B7)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
