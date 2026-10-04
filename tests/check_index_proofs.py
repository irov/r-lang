#!/usr/bin/env python3
"""Check which index places and integer conversions keep their check in the generated C.

A fixture marks each line that holds one index place with `/* proven */` (the emitter must
leave the bounds check out) or `/* checked */` (the check must stay), and each line that holds
one explicit integer conversion with `/* proven conversion */` or `/* checked conversion */`.
The panics of the C17 output carry the byte span of their index or conversion, which names
the line.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys

PANIC = re.compile(
    r'r_runtime_panic\(\s*R_RUNTIME_PANIC_(BOUNDS|INVALID_CONVERSION),\s*\(RRuntimeSourceSpan\)\{'
    r'UINT32_C\((\d+)\),\s*UINT32_C\((\d+)\),\s*UINT32_C\((\d+)\)\}\)')
MARK = re.compile(r'/\* (proven|checked)( conversion)? \*/\s*$')


def checked_lines(front, fixture):
    result = subprocess.run([front, '--emit=c17', str(fixture)],
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise SystemExit(f'{fixture.name}: r-front failed\n{result.stderr}')
    source = fixture.read_bytes()
    lines = set()
    for match in PANIC.finditer(result.stdout):
        # Source 1 is the fixture; library modules have other source numbers.
        if match.group(2) != '1':
            continue
        lines.add((match.group(1) == 'INVALID_CONVERSION',
                   source[:int(match.group(3))].count(b'\n') + 1))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('fixtures', nargs='+', type=Path)
    args = parser.parse_args()
    failures = []
    marks = 0
    for fixture in args.fixtures:
        checked = checked_lines(args.front, fixture)
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
