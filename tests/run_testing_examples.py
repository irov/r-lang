#!/usr/bin/env python3
"""Run the test program of examples/testing, built in test mode (Core R-FUNC-0025), and check
its report: one line per test in declaration order, the allocation runs of the allocation test,
the summary and the exit status."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
report = ('test parses_versions ... ok\n'
          'test rejects_a_missing_part ... ok\n'
          'test reports_the_offending_byte ... ok\n'
          'test orders_versions ... ok\n'
          'test formats_releases ... ok (2 allocation failures)\n'
          'test waits_for_a_release ... ok\n'
          '6 tests: 6 passed, 0 failed\n')
for _ in range(2):
    # The test entry takes no arguments; a second run gives the same report.
    result = subprocess.run([args.executable], text=True, capture_output=True, timeout=60)
    assert (result.returncode, result.stdout, result.stderr) == (0, report, ''), result
print('Testing: the report of 6 tests passed twice')
