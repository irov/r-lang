#!/usr/bin/env python3
"""Check that the fixtures of direct async calls compile and carry their marks (P4.4).

A fixture line that holds one awaited call ends with `/* direct */` (the generated code may run
the call directly, or complete the awaited receive at once, when the runtime allows it) or
`/* started */` (the call must keep its task).

The LLVM emitter does not take these paths yet: direct calls of async bodies return in stage B7,
and with them the comparison of each mark with the generated code. Until then this check
lowers every function of each fixture to LLVM IR and requires the marks that the comparison
will read; the codegen tests of the same fixtures run them as programs.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys

MARK = re.compile(r'/\* (direct|started) \*/\s*$')


def compile_fixture(front, library_map, fixture):
    command = [front, '--emit=llvm-ir', '--all-functions']
    if library_map is not None:
        command += ['--library-map', str(library_map)]
    result = subprocess.run(command + [str(fixture)], capture_output=True, text=True,
                            timeout=300)
    if result.returncode != 0:
        raise SystemExit(f'{fixture.name}: r-front failed\n{result.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', type=Path)
    parser.add_argument('fixtures', nargs='+', type=Path)
    args = parser.parse_args()
    marks = 0
    for fixture in args.fixtures:
        compile_fixture(args.front, args.library_map, fixture)
        marks += sum(1 for line in fixture.read_text().splitlines() if MARK.search(line))
    if marks == 0:
        print('no marked awaits', file=sys.stderr)
        return 1
    print(f'{marks} marked awaits compile '
          '(their checks against the generated code return in B7)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
