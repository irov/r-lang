#!/usr/bin/env python3
"""Print formatted device records, readings and endpoints through core::Format."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = 'status devices|readings|probe ADDRESS PORT\n'
devices = ('Device { id: 1, kind: sensor, endpoint: 10.0.0.7:5683, firmware: some(12) }\n'
           'Device { id: 2, kind: relay, endpoint: 10.0.0.9:502, firmware: none }\n'
           'Device { id: 3, kind: gateway, endpoint: [fd00::1]:8883, firmware: some(4) }\n')


def column(label, value):
    """A label padded to 10 and a value padded to 24 Unicode scalar values on the left."""
    return f'{label:>10} {value:>24}\n'


readings = (column('room', 'temperature(21.5 C)') +
            column('heater', 'switched { relay: 2, on: true }') +
            column('attic', 'missing') +
            'summary: 3, 21.5 C, false\n')
cases = [([], usage, 0),
         (['bogus'], usage, 64),
         (['devices'], devices, 0),
         (['devices', 'extra'], devices, 64),
         (['readings'], readings, 0),
         (['probe', '192.168.1.5', '8080'], column('endpoint', '192.168.1.5:8080'), 0),
         (['probe', '::1', '22'], column('endpoint', '[::1]:22'), 0),
         (['probe', '1.2.3.4'], 'status probe ADDRESS PORT\n', 64),
         (['probe', 'bad', '22'], 'status failed: invalid_character\n', 70),
         (['probe', '1.2.3.4', 'x'], 'status failed: invalid_digit\n', 70)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=60)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Status: {len(cases)} formatting checks passed')
