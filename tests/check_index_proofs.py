#!/usr/bin/env python3
"""Check which index places and integer conversions keep their check in the optimized program.

A fixture marks each line that holds one index place with `/* proven */` (no bounds check is left
for it) or `/* checked */` (its check stays), and each line that holds one explicit integer
conversion with `/* proven conversion */` or `/* checked conversion */`. The program is
optimized at the default level with every function kept as if called from elsewhere
(--all-functions), so a function is proven for its own arguments, not for the calls of main;
the checks left are read from the IR (tests/llvm_checks.py).
"""

import argparse
from pathlib import Path
import re
import sys

import llvm_checks

MARK = re.compile(r'/\* (proven|checked)( conversion)? \*/\s*$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('fixtures', nargs='+', type=Path)
    args = parser.parse_args()
    failures = []
    marks = 0
    for fixture in args.fixtures:
        checked = llvm_checks.checked_lines(llvm_checks.emit(args.front, fixture), fixture)
        for number, line in enumerate(fixture.read_text().splitlines(), start=1):
            match = MARK.search(line)
            if match is None:
                continue
            marks += 1
            conversion = match.group(2) is not None
            expected = match.group(1) == 'checked'
            present = (conversion, number) in checked
            if present != expected:
                kind = 'conversion' if conversion else 'bounds'
                state = f'has a {kind} check' if present else f'has no {kind} check'
                failures.append(
                    f'{fixture.name}:{number}: marked {match.group(0).strip()} but {state}')
    if marks == 0:
        failures.append('no marked lines')
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print(f'index proofs: {marks} marked index places and conversions match')
    return 0


if __name__ == '__main__':
    sys.exit(main())
