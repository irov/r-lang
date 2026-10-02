#!/usr/bin/env python3
"""Check a deterministic sensor report produced by two supervised borrowed tasks."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
cases = [(['-2', '12', '21'], 'count=2 sum=29 peak=19\n', 0),
         (['0'], '', 64),([], 'fanout OFFSET READING...\n', 0),
         (['0', '12', '-3', '21'], 'count=3 sum=30 peak=21\n', 0),
         (['0', '-7'], 'count=1 sum=-7 peak=-7\n', 0),
         (['0', '-9', '-2', '-4'], 'count=3 sum=-15 peak=-2\n', 0),
         (['0', '0', '0'], 'count=2 sum=0 peak=0\n', 0),
         (['0', 'bad'], '', 65), (['0', '9223372036854775808'], '', 65)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=20)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Fanout: {len(cases)} sensor-summary checks passed')
