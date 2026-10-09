#!/usr/bin/env python3
"""Check that the fixtures of loop versions compile and carry their marks (P4.4).

A fixture line that holds one index place ends with `/* versioned */` (its loop may check it
once before the loop and run a copy without the check when it holds) or `/* checked */` (the
check must stay in every iteration).

The LLVM emitter does not version loops yet: index proofs become facts for the optimizer in
stage B7, and the comparison of each mark with the loops of the generated code returns with
it. Until then this check lowers every function of each fixture to LLVM IR and requires the
marks that the comparison will read; the codegen tests of the same fixtures run them as
programs.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys

MARK = re.compile(r'/\* (versioned|checked) \*/\s*$')


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
        print('no marked index places', file=sys.stderr)
        return 1
    print(f'{marks} marked index places compile '
          '(their checks against the generated code return in B7)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
