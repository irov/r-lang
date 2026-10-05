#!/usr/bin/env python3
"""Run blocking C calls on the blocking call pool and check that the executor keeps running."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = 'offload nap|cancel MILLISECONDS | fault INDEX\n'
cases = [([], usage, 0),
         (['nap'], usage, 64),
         (['sleep', '10'], usage, 64),
         (['nap', '30'],
          '6 calls of 30 ms slept 180 ms\nthe ticker kept running: yes\n'
          'two rounds on four pool threads: yes\na call runs as a task of its own: yes\n', 0),
         (['cancel', '120'],
          'the timer won: yes\nthe group waited for the call to return: yes\n', 0),
         (['fault', '1'],
          'call 1 returned after 20 ms\nthe other call returned after 10 ms\n', 0),
         (['fault', '5'],
          'call 5 panicked: bounds\nthe other call returned after 10 ms\n', 0),
         (['nap', 'x'], 'offload failed: invalid_digit\n', 70)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=60)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Offload: {len(cases)} blocking call checks passed')
