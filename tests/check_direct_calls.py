#!/usr/bin/env python3
"""Check which awaits of a fixture may complete without their task.

A fixture line that holds one awaited call ends with `/* direct */` (the program may run the call
directly, or complete the awaited receive at once, when the runtime allows it) or `/* started */`
(the call keeps its task). The emitter lists the await of each such path in the named metadata
!r.direct of the IR, by its source key and bytes (tests/llvm_checks.py).
"""

import argparse
from pathlib import Path
import re
import sys

import llvm_checks

MARK = re.compile(r'/\* (direct|started) \*/\s*$')


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
        direct = llvm_checks.direct_lines(llvm_checks.emit(args.front, fixture, extra), fixture)
        marked = set()
        for number, line in enumerate(fixture.read_text().splitlines(), start=1):
            match = MARK.search(line)
            if match is None:
                continue
            marks += 1
            marked.add(number)
            expected = match.group(1) == 'direct'
            if (number in direct) != expected:
                state = 'may complete without its task' if number in direct else 'keeps its task'
                failures.append(f'{fixture.name}:{number}: marked {match.group(1)} but {state}')
        for number in sorted(direct - marked):
            failures.append(f'{fixture.name}:{number}: an unmarked await completes without its task')
    if marks == 0:
        failures.append('no marked awaits')
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f'direct calls: {marks} marked awaits agree with the program')
    return 0


if __name__ == '__main__':
    sys.exit(main())
