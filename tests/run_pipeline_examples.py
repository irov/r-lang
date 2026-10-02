#!/usr/bin/env python3
"""Check a generic pipeline whose stages throw their own checked errors."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
cases = [([], 'pipeline LIMIT FACTOR VALUE...\n', 0),
         (['10', '2', '3', '-4', '5'], 'count=3 sum=8 max=10\n', 0),
         (['10', '0', '3', '-4', '5'], 'count=3 sum=12 max=5\n', 0),
         (['5', '-3', '1', '2', '-5'], 'count=3 sum=6 max=15\n', 0),
         (['10', '2'], '', 64),
         (['x', '2', '3'], '', 65), (['10', '2', 'nine'], '', 65),
         (['10', '2', '3', '11'], '', 66), (['10', '0', '-11'], '', 66),
         (['10', '4611686018427387904', '3'], '', 67),
         (['9223372036854775807', '1', '9223372036854775807', '1'], '', 67)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=20)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Pipeline: {len(cases)} stage and checked-error checks passed')
