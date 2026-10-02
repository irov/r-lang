#!/usr/bin/env python3
"""Exercise logging controls using reflected enum order and owned JSON events."""
import argparse
import json
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0
    levels = ['trace', 'debug', 'info', 'warn', 'error']

    def run(*args, expected=None, status=0):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (args[:3], result.returncode, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args[:3], result.stdout, expected)
        count += 1
        return result.stdout

    run('levels', expected=''.join(f'{i} {name}\n' for i, name in enumerate(levels)) + 'levels=5 least=trace greatest=error\n')
    for index, level in enumerate(levels):
        run('next', level, expected=levels[min(index + 1, 4)] + '\n')
        run('filter', level, expected='accepted=0\n')
        args = [piece for candidate in levels for piece in [candidate, candidate + ' message']]
        expected = ''.join(f'[{name}] {name} message\n' for name in levels[index:]) + f'accepted={5-index}\n'
        run('filter', level, *args, expected=expected)
        for candidate_index, candidate in enumerate(levels):
            message = '\u00e9\U0001f642' if candidate_index % 2 else ''
            event = {'Message': {'level': candidate, 'text': message}}
            expected = f'Message: [{candidate}] {message}\n' if candidate_index >= index else 'Message: filtered\n'
            run('event', level, json.dumps(event), expected=expected)
        run('event', level, json.dumps({'Threshold': level}), expected=f'Threshold: threshold={level}\n')
        run('event', level, '"Quit"', expected='Quit: stopped\n')
    schema = run('schema').splitlines()
    assert len(schema) == 5 and schema[0].startswith('target=') and len(schema[0]) > 7, schema
    assert schema[1].startswith('profile=') and len(schema[1]) > 8, schema
    assert schema[2].startswith('type=') and 'Settings' in schema[2], schema
    assert schema[3:] == ['fields=3: threshold, retries, verbose', 'events=3'], schema
    for event in ['null', '1', '"Unknown"', '{}', '{"Threshold":"bad"}',
                  '{"Message":{"level":"warn"}}', '{"Message":{"level":"warn","text":null}}',
                  '{"Quit":0}', '{"Threshold":"info","Quit":null}']:
        run('event', 'info', event, status=65)
    for args in [('next',), ('next', 'bad'), ('filter',), ('filter', 'INFO'),
                 ('filter', 'info', 'warn'), ('filter', 'info', 'bad', 'message'),
                 ('event', 'info'), ('schema', 'extra'), ('levels', 'extra'), ('unknown',)]:
        run(*args, status=64)
    print(f'Logbook: {count} reflection, filtering, payload, JSON and validation checks passed')


if __name__ == '__main__':
    main()
