#!/usr/bin/env python3
"""B8.1: the DWARF of a program (--debug-info).

The IR names each function of the program by its R name in a subprogram, declares its
parameters and locals, and places its instructions on the lines of the R source, in an ordinary
and in an async function (whose step carries the locals of its frame), at level 0 and at the
default level; the object's DWARF passes llvm-dwarfdump --verify and its line table names the
source. Without the option the module carries no debug information.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def emit(front: Path, library_map: Path, source: Path, *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(front), '--profile=hosted-native-async', '--library-map', str(library_map), *extra,
         str(source)],
        capture_output=True, check=False)


def line_of(source: Path, text: str) -> int:
    for number, line in enumerate(source.read_text(encoding='utf-8').splitlines(), start=1):
        if text in line:
            return number
    raise AssertionError(f'{source.name} has no line with {text!r}')


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f'debug info check failed: {message}', file=sys.stderr)
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', type=Path, required=True)
    parser.add_argument('--library-map', type=Path, required=True)
    parser.add_argument('--dwarfdump', type=Path, required=True)
    parser.add_argument('--sync-fixture', type=Path, required=True)
    parser.add_argument('--async-fixture', type=Path, required=True)
    args = parser.parse_args()

    for level in ('0', '2'):
        result = emit(args.front, args.library_map, args.sync_fixture, '--emit=llvm-ir',
                      f'--opt-level={level}', '--debug-info')
        require(result.returncode == 0, f'level {level}: {result.stderr.decode()[:400]}')
        ir = result.stdout.decode()
        require('!llvm.dbg.cu' in ir, f'level {level}: no compile unit')
        for function in ('integers', 'owned', 'main'):
            require(f'DISubprogram(name: "test.codegen.dict_paths::{function}"' in ir,
                    f'level {level}: no subprogram of {function}')
    ir = emit(args.front, args.library_map, args.sync_fixture, '--emit=llvm-ir', '--opt-level=0',
              '--debug-info').stdout.decode()
    for variable in ('table', 'names', 'seven', 'index', 'key'):
        require(f'DILocalVariable(name: "{variable}"' in ir, f'no variable {variable}')
    require(re.search(r'DILocalVariable\(name: "left", arg: 1', ir) is not None,
            'no parameter left of same')
    seven = line_of(args.sync_fixture, 'std.string::string seven = ')
    require(re.search(rf'DILocation\(line: {seven}, column: \d+', ir) is not None,
            f'no location on line {seven}')
    require('DICompositeType(tag: DW_TAG_structure_type, name: "std.string::string"' in ir,
            'no structure of std.string::string')

    ir = emit(args.front, args.library_map, args.async_fixture, '--emit=llvm-ir', '--opt-level=0',
              '--debug-info').stdout.decode()
    require('DISubprogram(name: "test.codegen.async_dict_paths::integers$step"' in ir,
            'no subprogram of the async step')
    require('DILocalVariable(name: "removed_step"' in ir, 'no frame local of the async step')

    plain = emit(args.front, args.library_map, args.sync_fixture, '--emit=llvm-ir').stdout.decode()
    require('!dbg' not in plain and '!llvm.dbg.cu' not in plain,
            'debug information without --debug-info')

    with tempfile.TemporaryDirectory(prefix='r-debug-info-') as directory:
        objects = Path(directory)
        for fixture in (args.sync_fixture, args.async_fixture):
            output = objects / f'{fixture.stem}.o'
            result = emit(args.front, args.library_map, fixture, '--emit=object', '--debug-info')
            require(result.returncode == 0, result.stderr.decode()[:400])
            output.write_bytes(result.stdout)
            verify = subprocess.run([str(args.dwarfdump), '--verify', str(output)],
                                    capture_output=True, text=True, check=False)
            require(verify.returncode == 0 and 'No errors.' in verify.stdout,
                    f'{fixture.name}: {verify.stdout[-400:]}{verify.stderr[-400:]}')
            lines = subprocess.run([str(args.dwarfdump), '--debug-line', str(output)],
                                   capture_output=True, text=True, check=False)
            require(fixture.name in lines.stdout, f'{fixture.name}: the line table names no source')
    print('debug info checks passed')


if __name__ == '__main__':
    main()
