#!/usr/bin/env python3
"""Mutation testing of R source (B8.2).

Each mutant changes one operator or literal of an R source: a comparison (`<` and `<=`, `>` and
`>=`, `==` and `!=`), a binary `+` or `-` and its compound assignment, `&&` and `||`, `true` and
`false`, or an integer literal (n to n + 1). Comments, strings and characters are not touched, and
`<`, `>`, `+` and `-` only where spaces surround them, as the binary operators of R are written, so
that generic brackets and signs stay whole. A mutant is built and run by the command of a CTest
test of the build (`ctest --show-only=json-v1`): the test of a fixture with the fixture replaced,
the test of a library module (`r_frontend_codegen_rtest_<module>`) with the module replaced in a
copy of the library map. The test passing means the mutant survived; failing, that it was
killed; a mutant that does not translate is stillborn. A gcov report of the source
(`llvm-cov gcov -t`, B8.2) limits the mutants to the lines that ran.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


@dataclass(frozen=True)
class Mutant:
    line: int
    column: int
    original: str
    replacement: str
    offset: int


SWAPS = {
    '<': '<=', '<=': '<', '>': '>=', '>=': '>', '==': '!=', '!=': '==',
    '+': '-', '-': '+', '+=': '-=', '-=': '+=', '&&': '||', '||': '&&',
}
SPACED = {'<', '>', '+', '-', '<=', '>=', '+=', '-='}
TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\''
    r'|(?P<word>\b(?:true|false)\b)'
    r'|(?P<number>\b\d[\d_]*(?:[iu](?:8|16|32|64|size))?\b)'
    r'|(?P<operator>&&|\|\||==|!=|<=|>=|\+=|-=|<|>|\+|-)',
    re.DOTALL)


def mutants(text: str) -> list[Mutant]:
    found: list[Mutant] = []
    for match in TOKEN.finditer(text):
        start = match.start()
        line = text.count('\n', 0, start) + 1
        column = start - (text.rfind('\n', 0, start) + 1) + 1
        if match.group('word') is not None:
            word = match.group('word')
            found.append(Mutant(line, column, word, 'false' if word == 'true' else 'true', start))
        elif match.group('number') is not None:
            literal = match.group('number')
            digits = re.match(r'[\d_]+', literal).group(0)
            suffix = literal[len(digits):]
            # A literal glued to a name or a dot (an identifier, a tuple index) is no number.
            if start > 0 and (text[start - 1].isalnum() or text[start - 1] in '_.'):
                continue
            value = int(digits.replace('_', ''))
            found.append(Mutant(line, column, literal, f'{value + 1}{suffix}', start))
        elif match.group('operator') is not None:
            operator = match.group('operator')
            end = match.end()
            if operator in SPACED and not (
                    start > 0 and text[start - 1] == ' ' and end < len(text) and text[end] == ' '):
                continue
            found.append(Mutant(line, column, operator, SWAPS[operator], start))
    return found


def apply(text: str, mutant: Mutant) -> str:
    return text[:mutant.offset] + mutant.replacement + text[mutant.offset + len(mutant.original):]


def covered_lines(report: Path) -> set[int]:
    """The lines a gcov report (llvm-cov gcov -t) shows as run."""
    lines: set[int] = set()
    for raw in report.read_text(encoding='utf-8').splitlines():
        parts = raw.split(':', 2)
        if len(parts) == 3 and parts[0].strip() not in ('-', '#####', '=====') and \
                parts[0].strip().rstrip('*').isdigit():
            lines.add(int(parts[1].strip()))
    return lines


def test_command(ctest: str, build: Path, name: str) -> tuple[list[str], str]:
    listing = subprocess.run(
        [ctest, '--test-dir', str(build), '-R', f'^{re.escape(name)}$', '--show-only=json-v1'],
        capture_output=True, text=True, check=True)
    tests = json.loads(listing.stdout)['tests']
    if len(tests) != 1:
        raise SystemExit(f'no single test {name} in {build}')
    directory = next((item['value'] for item in tests[0].get('properties', [])
                      if item['name'] == 'WORKING_DIRECTORY'), str(build))
    return tests[0]['command'], directory


def library_map_copy(map_path: Path, module: str, replacement: Path, destination: Path) -> None:
    lines = []
    for raw in map_path.read_text(encoding='utf-8').splitlines():
        body = raw.split('#', 1)[0].strip()
        if not body:
            lines.append(raw)
            continue
        name, rest = (part.strip() for part in body.split('=', 1))
        parts = rest.split()
        path = replacement if name == module else (map_path.parent / parts[0]).resolve()
        lines.append(' '.join([f'{name} = {path}', *parts[1:]]))
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def run_mutant(command: list[str], directory: str, source: Path, module: str | None,
               mutant: Mutant, text: str, scratch: Path, index: int, timeout: float) -> str:
    folder = scratch / f'mutant-{index}'
    folder.mkdir()
    mutated = folder / source.name
    mutated.write_text(apply(text, mutant), encoding='utf-8')
    arguments = []
    for argument in command:
        if argument.startswith('-DOUTPUT_EXE='):
            argument = f'-DOUTPUT_EXE={folder / "program"}'
        elif module is None and argument.startswith('-DSOURCE_1='):
            argument = f'-DSOURCE_1={mutated}'
        elif module is not None and argument.startswith('-DLIBRARY_MAP='):
            copy = folder / 'library.map'
            library_map_copy(Path(argument.split('=', 1)[1]), module, mutated, copy)
            argument = f'-DLIBRARY_MAP={copy}'
        arguments.append(argument)
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, cwd=directory,
                                timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return 'killed'
    if result.returncode == 0:
        return 'survived'
    output = result.stdout + result.stderr
    if 'r-front --emit=object failed' in output or 'r-front --test' in output and 'failed' in output:
        return 'stillborn'
    return 'killed'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True, help='a configured build tree')
    parser.add_argument('--test', required=True, help='the CTest test that builds and runs it')
    parser.add_argument('--source', type=Path, required=True, help='the R source to mutate')
    parser.add_argument('--module', help='the library module the source is (std.text)')
    parser.add_argument('--coverage-report', type=Path, help='llvm-cov gcov -t of the source')
    parser.add_argument('--lines', help='FIRST-LAST: mutate only these lines')
    parser.add_argument('--ctest', default='ctest', help='the ctest of the build')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--timeout', type=float, default=600.0)
    parser.add_argument('--output', type=Path, help='the JSON report')
    args = parser.parse_args()

    source = args.source.resolve()
    text = source.read_text(encoding='utf-8')
    candidates = mutants(text)
    if args.coverage_report is not None:
        run = covered_lines(args.coverage_report)
        candidates = [mutant for mutant in candidates if mutant.line in run]
    if args.lines is not None:
        first, last = (int(part) for part in args.lines.split('-', 1))
        candidates = [mutant for mutant in candidates if first <= mutant.line <= last]
    command, directory = test_command(args.ctest, args.build, args.test)
    with tempfile.TemporaryDirectory(prefix='r-mutants-') as scratch:
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            outcomes = list(executor.map(
                lambda item: run_mutant(command, directory, source, args.module, item[1], text,
                                        Path(scratch), item[0], args.timeout),
                enumerate(candidates)))
    counts = {kind: outcomes.count(kind) for kind in ('killed', 'survived', 'stillborn')}
    survivors = [f'{source.name}:{mutant.line}:{mutant.column}: {mutant.original} -> '
                 f'{mutant.replacement}'
                 for mutant, outcome in zip(candidates, outcomes) if outcome == 'survived']
    report = {'source': str(source), 'test': args.test, 'mutants': len(candidates), **counts,
              'survivors': survivors}
    if args.output is not None:
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'{source.name}: {len(candidates)} mutants, {counts["killed"]} killed, '
          f'{counts["survived"]} survived, {counts["stillborn"]} stillborn')
    for survivor in survivors:
        print(f'  survived {survivor}')


if __name__ == '__main__':
    main()
