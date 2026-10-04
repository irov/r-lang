#!/usr/bin/env python3
"""Check which index places the generated C checks once before their loop (P4.4).

A fixture line that holds one index place ends with `/* versioned */` (its loop must check it
once before the loop and run a copy without the check when it holds) or `/* checked */` (the
check must stay in every iteration). The emitter introduces each versioned loop with a comment
naming the source bytes of each such index.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys

VERSIONED = re.compile(
    r'/\* Checked once below: source (\d+), bytes (\d+)\.\.(\d+) \(P4\.4\)\. \*/')
MARK = re.compile(r'/\* (versioned|checked) \*/\s*$')


def versioned_lines(front, library_map, fixture):
    command = [front, '--emit=c17']
    if library_map is not None:
        command += ['--library-map', str(library_map)]
    result = subprocess.run(command + [str(fixture)], capture_output=True, text=True,
                            timeout=300)
    if result.returncode != 0:
        raise SystemExit(f'{fixture.name}: r-front failed\n{result.stderr}')
    source = fixture.read_bytes()
    by_ordinal = {}
    for match in VERSIONED.finditer(result.stdout):
        by_ordinal.setdefault(match.group(1), []).append(
            (int(match.group(2)), int(match.group(3))))
    # Library modules are other sources; the fixture is the one whose spans all name an index.
    fixture_spans = [
        spans for spans in by_ordinal.values()
        if all(end <= len(source) and b'[' in source[start:end] for start, end in spans)]
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
        versioned = versioned_lines(args.front, args.library_map, fixture)
        marked = set()
        for number, line in enumerate(fixture.read_text().splitlines(), start=1):
            match = MARK.search(line)
            if match is None:
                continue
            marks += 1
            marked.add(number)
            expected = match.group(1) == 'versioned'
            if (number in versioned) != expected:
                state = 'is checked once' if number in versioned else 'keeps its check'
                failures.append(f'{fixture.name}:{number}: marked {match.group(1)} but {state}')
        for number in sorted(versioned - marked):
            failures.append(f'{fixture.name}:{number}: an unmarked index is checked once')
    if marks == 0:
        failures.append('no marked index places')
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f'{marks} marked index places agree with the generated C')
    return 0


if __name__ == '__main__':
    sys.exit(main())
