#!/usr/bin/env python3
"""Check the stack of the runtime unit written in R (Core R-FFI-0068, B12.3): the compiler
measures every entry of the unit, its exports and internal callbacks, and the runtime entries of
the stack inventory whose source is a module of the unit are entries it measured, each within the
call transition of the target (tools/generate_runtime_entry_stack.py --verify-unit-stack).

Usage: check_runtime_unit_stack.py --front R_FRONT --root ROOT --output-dir DIR -- UNIT_ARGUMENTS
SOURCES..."""

import argparse
from pathlib import Path
import subprocess
import sys

INVENTORY = "targets/arm64-apple-darwin.hosted-native-async.runtime-entry-stack.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('unit', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    unit = args.unit[1:] if args.unit[:1] == ['--'] else args.unit
    root = Path(args.root)
    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=True)
    record = output / 'core.abi.json'
    module = output / 'core.ll'
    for command in (
            [args.front, '--emit=abi-record', *unit, '--output', str(record)],
            [args.front, '--emit=llvm-ir', *unit, '--abi-record', str(record), '--output',
             str(module)]):
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        assert result.returncode == 0, result.stderr
    text = module.read_text(encoding='utf-8')
    assert '!r.stack.entries' in text, 'the unit reports no measured entries'
    result = subprocess.run(
        [sys.executable, str(root / 'tools/generate_runtime_entry_stack.py'), '--root', str(root),
         '--inventory', str(root / INVENTORY), '--verify-unit-stack', '--unit-ir', str(module)],
        capture_output=True, text=True, timeout=600)
    assert result.returncode == 0, result.stderr
    print('runtime unit stack checks passed')


if __name__ == '__main__':
    main()
