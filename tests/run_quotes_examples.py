#!/usr/bin/env python3
"""Check cached tax policy, integer rounding and concurrent quote snapshots."""
import argparse
import os
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    baseline = {key: value for key, value in os.environ.items() if key not in ['R_QUOTES_ENABLED', 'R_QUOTES_TAX']}
    count = 0

    def run(*args, expected=None, status=0, environment=None):
        nonlocal count
        env = baseline | (environment or {})
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=10, env=env)
        assert result.returncode == status and not result.stderr, (args[:3], result.returncode, result.stdout, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args[:3], result.stdout, expected)
        count += 1
        return result.stdout

    def oracle(values, rate, explicit=False):
        lines = []
        previous = 0
        for revision, net in enumerate(values, 1):
            gross = net + (net * rate + 5000) // 10000
            lines.append(f'revision={revision} net={net} gross={gross} previous={previous}\n')
            previous = gross
        lines.append(f'tax_basis_points={rate}\n' if values or explicit else 'no prices\n')
        return ''.join(lines)

    rng = random.Random(863)
    for values in [[], [0], [1, 2, 3, 5, 99, 100], [1000000000000],
                   [rng.randrange(1000000) for _ in range(75)]]:
        for command in ['prices', 'force']:
            run(command, *values, expected=oracle(values, 2000))
        for rate in [0, 1, 750, 10000]:
            run('rate', rate, *values, expected=oracle(values, rate, True), environment={'R_QUOTES_TAX': 'ignored'})
    for rate in [1, 1750, 9999]:
        run('prices', 3, 500, 15000, expected=oracle([3, 500, 15000], rate), environment={'R_QUOTES_TAX': str(rate), 'R_QUOTES_ENABLED': 'yes'})
    for values in [[], [0], [100], list(range(200)), [1000000000000]] * 3:
        lines = run('watch', *values).splitlines()
        fields = dict(field.split('=') for field in lines[0].split())
        last = values[-1] if values else 0
        assert int(fields['revision']) == len(values)
        assert int(fields['gross']) == last + (last * 2000 + 5000) // 10000
        assert int(fields['snapshots']) >= 0
        assert lines[1] == ('tax_basis_points=2000' if values else 'no prices')
    for command in ['prices', 'force', 'watch']:
        run(command, 1, status=78, expected='quote policy rejected\n', environment={'R_QUOTES_ENABLED': 'no'})
        run(command, status=0, environment={'R_QUOTES_ENABLED': 'no'})
    run('prices', 1, status=78, environment={'R_QUOTES_TAX': '10001'})
    run('prices', 1, status=65, environment={'R_QUOTES_TAX': 'bad'})
    run('rate', 10001, 1, status=78)
    for args in [('rate',), ('unknown',), ('prices', 1000000000001)]:
        run(*args, status=64)
    for args in [('prices', '-1'), ('prices', '18446744073709551616'), ('rate', '-1', 1)]:
        run(*args, status=65)
    print(f'Quotes: {count} policy, rounding, cache, publication and validation checks passed')


if __name__ == '__main__':
    main()
