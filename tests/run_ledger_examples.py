#!/usr/bin/env python3
"""Check concurrent posting and acknowledged condition-variable handoff."""
import argparse
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, expected=None, status=0):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (args[:3], result.returncode, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args[:3], result.stdout, expected)
        count += 1

    rng = random.Random(531)
    cases = [[], [0], [100, -20, 7, -3], [-2147483648, 2147483647], [2147483647] * 64,
             [rng.randint(-2147483648, 2147483647) for _ in range(257)]]
    for values in cases * 4:
        run('batch', *values, expected=f'balance={sum(values)} entries={len(values)}\n')
    for value in [0, 1, -1, -9223372036854775808, 9223372036854775807] * 6:
        run('handoff', value, expected=f'received={value} acknowledged=true\n')
    for args in [('handoff',), ('handoff', 1, 2), ('bad',)]:
        run(*args, status=64)
    for args in [('batch', '2147483648'), ('batch', '-2147483649'), ('batch', 'bad'),
                 ('handoff', '9223372036854775808')]:
        run(*args, status=65)
    print(f'Ledger: {count} posting, handoff and validation checks passed')


if __name__ == '__main__':
    main()
