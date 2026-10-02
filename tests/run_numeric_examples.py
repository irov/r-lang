#!/usr/bin/env python3
"""Exercise every numeric calculator operation and check results against integer arithmetic."""
from __future__ import annotations
import argparse
import struct
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from generate_numeric_example import R_INTEGERS, C_INTEGERS, FLOATS


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def check(args: list[str], expected: str | None, status: int = 0) -> None:
        nonlocal count
        run = subprocess.run([exe, *args], capture_output=True, text=True, timeout=10)
        assert run.returncode == status and not run.stderr, (args, run.returncode, run.stdout, run.stderr)
        assert run.stdout.strip() == expected if expected is not None else bool(run.stdout.strip()), (args, run.stdout, expected)
        count += 1

    for kind in R_INTEGERS + C_INTEGERS:
        check(['parse', kind, '1', '2'], '1')
        check(['convert', kind, '1'], '1')
        if kind != 'c_bool':
            check(['parse', kind, '7f', '16'], '7f')
            check(['parse', kind, '0x7f', '0'], '127')
        if kind in C_INTEGERS:
            check(['c_convert', kind, '1'], '1')
    for kind in FLOATS:
        check(['parse', kind, '1.25'], '1.25')
        check(['convert', kind, '1.25'], '1.25')
        if kind.startswith('c_'):
            check(['c_convert', kind, '1.25'], '1.25')
    pointer_bits = struct.calcsize('P') * 8
    for kind in R_INTEGERS:
        signed = kind.startswith('i')
        bits = pointer_bits if kind in {'isize', 'usize'} else int(kind[1:])
        low = -(1 << (bits - 1)) if signed else 0
        high = (1 << (bits - (1 if signed else 0))) - 1
        check(['limits', kind], f'{low} {high}')
        for op, fn, a, b in [('add', lambda x, y: x+y, high, 1),
                              ('sub', lambda x, y: x-y, low, 1),
                              ('mul', lambda x, y: x*y, high, 2)]:
            for left, right in [(6, 2), (a, b)]:
                result = fn(left, right)
                wrapped = ((result - low) % (1 << bits)) + low
                saturated = min(high, max(low, result))
                checked = str(result) if low <= result <= high else 'overflow'
                check(['checked_'+op, kind, str(left), str(right)], checked)
                check(['wrapping_'+op, kind, str(left), str(right)], str(wrapped))
                check(['saturating_'+op, kind, str(left), str(right)], str(saturated))
        check(['parse', kind, str(high+1)], None, 65)
        check(['parse', kind, str(low-1)], None, 65)
    for args, status in [(['convert','u8','256'],65), (['convert','i32','1.5'],65),
                         (['parse','u8','2','2'],65), (['parse','u8','1','1'],65),
                         (['parse','unknown','1'],64), (['checked_add','u8','1'],64),
                         (['checked_add','c_int','1','2'],64), (['c_convert','i32','1'],64)]:
        check(args, None, status)
    check([], None)
    print(f'Numbers: {count} commands passed across every integer and conversion specialization')


if __name__ == '__main__':
    main()
