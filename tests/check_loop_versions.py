#!/usr/bin/env python3
"""Check which index places of a fixture are versioned.

A fixture line that holds one index place ends with `/* versioned */` (its check is written as
`index >= len && !fits`, where `fits` tests the largest value of the index below the test of its
loop, so that the optimizer may run a copy of the loop without the check) or `/* checked */` (the
check stays as written). The emitter lists each versioned index in the named metadata
!r.versioned of the IR, by its source key and bytes (tests/llvm_checks.py,
compiler/llvm/versions.c).
"""

import argparse
from pathlib import Path
import re
import sys

import llvm_checks

MARK = re.compile(r'/\* (versioned|checked) \*/\s*$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', type=Path)
    parser.add_argument('fixtures', nargs='+', type=Path)
    args = parser.parse_args()
    failures = []
    marks = 0
    for fixture in args.fixtures:
        extra = [] if args.library_map is None else ['--library-map', str(args.library_map)]
        versioned = llvm_checks.listed_lines(
            llvm_checks.emit(args.front, fixture, extra), fixture, 'r.versioned')
        marked = set()
        for number, line in enumerate(fixture.read_text().splitlines(), start=1):
            match = MARK.search(line)
            if match is None:
                continue
            marks += 1
            marked.add(number)
            expected = match.group(1) == 'versioned'
            if (number in versioned) != expected:
                state = 'is versioned' if number in versioned else 'keeps its plain check'
                failures.append(f'{fixture.name}:{number}: marked {match.group(1)} but {state}')
        for number in sorted(versioned - marked):
            failures.append(f'{fixture.name}:{number}: an unmarked index is versioned')
    if marks == 0:
        failures.append('no marked index places')
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f'loop versions: {marks} marked index places agree with the program')
    return 0


if __name__ == '__main__':
    sys.exit(main())
