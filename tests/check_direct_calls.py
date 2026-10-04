#!/usr/bin/env python3
"""Check which awaits of a fixture complete without their task in the generated C (P4.4).

A fixture line that holds one awaited call ends with `/* direct */` (the C17 output must run the
call directly, or complete the awaited receive at once, when the runtime allows it) or
`/* started */` (the call must keep its task). The emitter introduces each such path with a
comment naming the source bytes of its await.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys

DIRECT = re.compile(
    r'/\* (?:Direct call|Synchronous receive) of the await at source (\d+), '
    r'bytes (\d+)\.\.(\d+) \(P4\.4\)\. \*/')
MARK = re.compile(r'/\* (direct|started) \*/\s*$')


def direct_lines(front, library_map, fixture):
    command = [front, '--emit=c17']
    if library_map is not None:
        command += ['--library-map', str(library_map)]
    result = subprocess.run(command + [str(fixture)], capture_output=True, text=True,
                            timeout=300)
    if result.returncode != 0:
        raise SystemExit(f'{fixture.name}: r-front failed\n{result.stderr}')
    source = fixture.read_bytes()
    by_ordinal = {}
    for match in DIRECT.finditer(result.stdout):
        start, end = int(match.group(2)), int(match.group(3))
        by_ordinal.setdefault(match.group(1), []).append((start, end))
    # Library modules are other sources; the fixture is the one whose spans all name an await.
    fixture_spans = [
        spans for spans in by_ordinal.values()
        if all(end <= len(source) and source[start:end].startswith(b'await ')
               for start, end in spans)]
    if len(fixture_spans) > 1:
        raise SystemExit(f'{fixture.name}: cannot tell the source of the fixture')
    return {source[:start].count(b'\n') + 1
            for spans in fixture_spans for start, _ in spans}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', type=Path)
    parser.add_argument('fixtures', nargs='+', type=Path)
    args = parser.parse_args()
    failures = []
    marks = 0
    for fixture in args.fixtures:
        direct = direct_lines(args.front, args.library_map, fixture)
        marked = set()
        for number, line in enumerate(fixture.read_text().splitlines(), start=1):
            match = MARK.search(line)
            if match is None:
                continue
            marks += 1
            marked.add(number)
            expected = match.group(1) == 'direct'
            if (number in direct) != expected:
                state = 'runs directly' if number in direct else 'keeps its task'
                failures.append(f'{fixture.name}:{number}: marked {match.group(1)} but {state}')
        for number in sorted(direct - marked):
            failures.append(f'{fixture.name}:{number}: an unmarked await runs directly')
    if marks == 0:
        failures.append('no marked awaits')
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f'{marks} marked awaits agree with the generated C')
    return 0


if __name__ == '__main__':
    sys.exit(main())
