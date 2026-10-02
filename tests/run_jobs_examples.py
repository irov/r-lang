#!/usr/bin/env python3
"""Check generic key deduplication and task payload cleanup against Python CRC32."""
import argparse
import random
import subprocess
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, status=0):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=12)
        assert result.returncode == status and not result.stderr, (args[:3], result.returncode, result.stdout, result.stderr)
        count += 1
        return result.stdout

    rng = random.Random(381)
    for ids in [[], [1], [1, 1, 2, 1, 2], [0, 2**32-1, 0]] + [
            [rng.randrange(50) for _ in range(60)] for _ in range(20)]:
        lines = run('dispatch', *ids).splitlines()
        assert len(lines) == len(ids) + 1
        seen = set()
        for number, line in zip(ids, lines):
            if number in seen:
                assert line == f'duplicate={number}', line
            else:
                fields = dict(x.split('=') for x in line.split())
                assert int(fields['accepted']) == number and 0 <= int(fields['shard']) < 4, line
            seen.add(number)
        assert lines[-1] == f'jobs={len(seen)}'
    for mode in ['wait', 'detach', 'cancel']:
        for delay, payload in [(0, ''), (1, 'a'), (20, '123456789'), (1000 if mode == 'cancel' else 5, '\u00e9'*100)] * 3:
            fields = dict(x.split('=') for x in run(mode, delay, payload).split())
            assert fields['released'] == '1' and fields['finalized'] == '1' and fields['failed'] == '0', fields
            checksum = zlib.crc32(payload.encode())
            assert int(fields['crc32']) in ([0, checksum] if mode == 'cancel' else [checksum]), fields
    for args in [('unknown',), ('wait',), ('wait', 5001, 'x'), ('detach', 1), ('cancel', 0)]:
        run(*args, status=64)
    for args in [('dispatch', -1), ('dispatch', 2**32), ('wait', 'bad', 'x')]:
        run(*args, status=65)
    print(f'Jobs: {count} deduplication, completion, detachment, cancellation and cleanup checks passed')


if __name__ == '__main__':
    main()
