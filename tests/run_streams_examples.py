#!/usr/bin/env python3
"""Check that scoped I/O operations relay standard input through a file, TCP and UDP."""
import argparse
import os
import subprocess
import tempfile
import zlib

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()


def run(*arguments, data=b'', status=0, expected=None):
    result = subprocess.run([args.executable, *arguments], input=data, capture_output=True, timeout=60)
    assert result.returncode == status and result.stderr == b'', (arguments, result)
    if expected is not None:
        assert result.stdout == expected, (arguments, result.stdout, expected)


with tempfile.TemporaryDirectory(prefix='r-streams-') as directory:
    target = os.path.join(directory, 'relay.bin')
    checks = 0
    for capacity, data in ((64, b'scoped bytes'), (32, bytes(range(32))), (16, b''), (8, b'0123456789abcdef')):
        placed = data[:capacity]
        udp_capacity = capacity // 2 + 1
        udp = placed[:udp_capacity]
        report = (f'stdin={len(placed)} crc32={zlib.crc32(placed)}\n'
                  f'file={len(placed)} crc32={zlib.crc32(placed)}\n'
                  f'tcp={len(placed)} crc32={zlib.crc32(placed)}\n'
                  f'udp={len(udp)} truncated={"true" if len(placed) > udp_capacity else "false"} crc32={zlib.crc32(udp)}\n')
        run(target, str(capacity), data=data, expected=report.encode())
        with open(target, 'rb') as stored:
            assert stored.read() == placed, (capacity, data)
        checks += 1
    run(target, '0', status=64, expected=b'capacity must be in 1..65536\n')
    run(expected=b'streams FILE CAPACITY\n')
    run(target, status=64, expected=b'streams FILE CAPACITY\n')
    run(target, 'many', status=65)
    checks += 4
print(f'Streams: {checks} scoped I/O checks passed')
