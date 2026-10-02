#!/usr/bin/env python3
"""Exercise native producer/consumer queues, scoped borrows and park notifications."""
import argparse
import random
import subprocess
import zlib


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
        return result.stdout

    for messages in [[], [''], ['hello', 'world'], ['caf\u00e9', '\U0001f642', 'x' * 4096],
                     [f'job-{i}' for i in range(96)]]:
        expected = ''.join(f'{i}: bytes={len(text.encode())} crc32={zlib.crc32(text.encode())}\n'
                           for i, text in enumerate(messages))
        run('queue', *messages, expected=expected)
        run('detached', *messages, expected=expected)
        for capacity in [0, 1, 4, 4096]:
            run('bounded', capacity, *messages, expected=expected)
    randomizer = random.Random(713)
    for values in [[], [0], [4294967295] * 7, [1, 2, 3],
                   [randomizer.randrange(4294967296) for _ in range(129)]]:
        run('scoped', *values, expected=f'sum={sum(values)}\n')
        run('owned', *values, expected=f'sum={sum(values)}\n')
    for delay in [0, 1, 10, 30] * 3:
        run('park', delay, expected='ready\n')
    for args in [('park',), ('park', 1001), ('bounded',), ('bounded', 4097), ('unknown',)]:
        run(*args, status=64)
    run('scoped', '4294967296', status=65)
    run('owned', '4294967296', status=65)
    run('park', '-1', status=65)
    print(f'Workers: {count} queue, rendezvous, scoped sum, notification and validation checks passed')


if __name__ == '__main__':
    main()
