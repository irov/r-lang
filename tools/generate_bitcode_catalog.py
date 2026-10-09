#!/usr/bin/env python3
"""Write the bitcode catalog r-front reads with --bitcode-catalog (B7.2 of the LLVM transition).

Each bitcode object of the runtime and library C (the bitcode twins of their CMake targets)
defines some external functions; the catalog has one line "SYMBOL<TAB>PATH" per function, so
r-front can find the module that defines a function a program calls. A symbol that two
different sources define is an error: the program would link one of the two definitions.
"""

import argparse
from pathlib import Path
import subprocess
import sys


def defined_symbols(nm, path):
    result = subprocess.run([nm, '--defined-only', '--extern-only', '--format=just-symbols',
                             str(path)], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f'{nm} failed on {path}:\n{result.stderr}')
    # Mach-O names carry the C prefix underscore.
    return [line[1:] if line.startswith('_') else line
            for line in result.stdout.split() if line]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('objects', nargs='*', type=Path)
    args = parser.parse_args()
    owners = {}
    for path in args.objects:
        for symbol in defined_symbols(args.nm, path):
            if symbol in owners:
                # One source compiled into two targets defines its symbols twice alike.
                if owners[symbol].name != path.name:
                    print(f'{symbol} is defined by {owners[symbol]} and {path}', file=sys.stderr)
                    return 1
                continue
            owners[symbol] = path
    text = ''.join(f'{symbol}\t{owners[symbol].resolve()}\n' for symbol in sorted(owners))
    if not args.output.exists() or args.output.read_text() != text:
        args.output.write_text(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
