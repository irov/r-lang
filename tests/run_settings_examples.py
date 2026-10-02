#!/usr/bin/env python3
"""Check settings failures reported through an error family and the standard error root."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
cases = [([], 'settings KEY VALUE...\n', 0),
         (['port', '9000', 'workers', '8', 'mode', 'fast'],
          'port=9000 workers=8 mode=fast retries=3\n', 0),
         (['retries', '2', 'mode', 'safe'], 'port=8080 workers=4 mode=safe retries=2\n', 0),
         (['port'], 'argument 1: port has no value\n', 65),
         (['color', 'red'], 'argument 1: unknown key color\n', 65),
         (['workers', 'x'], 'argument 2: workers shall be a decimal integer\n', 65),
         (['port', '70000'], 'argument 2: port shall be from 1 to 65535\n', 65),
         (['mode', 'slow'], 'argument 2: mode shall be fast or safe\n', 65),
         (['workers', '2', 'retries', '300'], 'standard failure: above_maximum\n', 70)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=20)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Settings: {len(cases)} argument and failure checks passed')
