#!/usr/bin/env python3
"""Check a report whose columns are a tuple spread into a function over a type pack."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
cases = [([], 'tally VALUE...\n', 0),
         (['1', '2', '3'], 'count=3  sum=6  min=1  max=3  even=yes\n', 0),
         (['5', '-2'], 'count=2  sum=3  min=-2  max=5  even=no\n', 0),
         (['-7'], 'count=1  sum=-7  min=-7  max=-7  even=no\n', 0),
         (['4', 'x'], '', 65)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=20)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Tally: {len(cases)} column and argument checks passed')
