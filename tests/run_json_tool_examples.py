#!/usr/bin/env python3
"""Compare JSON editing, schemas and incremental input with independent Python models."""
import argparse
from decimal import Decimal
import json
import subprocess
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, status=0, data=None):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], input=data, capture_output=True, timeout=15)
        assert result.returncode == status and not result.stderr, (args, result)
        count += 1
        return result.stdout.decode()

    def encoded(value):
        return json.dumps(value, ensure_ascii=False, separators=(',', ':'))

    def check(args, value):
        text = run(*args)
        assert json.loads(text) == value, (args, text, value)
        return text

    values = [None, True, False, 0, -123, '', 'caf\u00e9 \U0001f642', [], {},
              [1, True, None, 'x'], {'items': [1, None, 'Madrid'], 'enabled': False}]
    for value in values:
        source = encoded(value)
        check(('compact', source), value)
        for indent in [0, 2, 8]:
            check(('pretty', source, indent), value)
    source = {'name': 'Ada', 'items': [1, 2, 3], 'enabled': False}
    check(('keys', encoded(source)), list(source))
    check(('get', encoded(source), 'items'), [1, 2, 3])
    check(('get', encoded(source), 'missing'), None)
    check(('set', encoded(source), 'enabled', 'true'), {**source, 'enabled': True})
    check(('take', encoded(source), 'name'), {'taken': 'Ada', 'remaining': {'items': [1, 2, 3], 'enabled': False}})
    check(('index', '[1,2,3]', 1), 2)
    check(('index', '[1,2,3]', 99), None)
    check(('append', '[1,2]', '{"x":true}'), [1, 2, {'x': True}])
    check(('shift', '[1,2,3]', 1), {'taken': 2, 'remaining': [1, 3]})
    document = {'items': ['Madrid', True, None, 42], 'other': [False, '\u00e9']}
    check(('stats', encoded(document)), {'nodes': 9, 'nulls': 1, 'booleans': 2,
          'enabled': 1, 'numbers': 1, 'strings': 2, 'text_bytes': 8, 'arrays': 2, 'objects': 1})
    for number in ['0', '-0.00e-999999999999', '18446744073709551615', '-9223372036854775808', '1e5000', '1.234567890123456789']:
        text = run('number', number)
        result = json.loads(text, parse_float=Decimal)
        assert result['spelling'] == number and result['zero'] == (Decimal(number) == 0), text
        assert result['value'] == Decimal(number), text
    basic = {'Display-Name': 'Ada', 'city': 'Madrid'}
    normalized = {'display_name': 'Ada', 'city': 'Madrid', 'retries': 3}
    check(('config', encoded(basic)), normalized)
    check(('strict', encoded(basic)), normalized)
    check(('config', encoded({**basic, 'id': '18446744073709551615', 'token': '', 'email': '', 'verbose': False})),
          {'id': '18446744073709551615', **normalized, 'token': ''})
    check(('config', encoded({**basic, 'id': '0', 'token': None, 'verbose': True, 'email': 'ada@example.test', 'retries': 0})),
          {'display_name': 'Ada', 'city': 'Madrid', 'email': 'ada@example.test', 'retries': 0, 'verbose': True})
    check(('config', encoded({**basic, 'unknown': 5, 'origin': 'ignored'})), normalized)
    check(('snapshot', encoded({'configuration': basic})), {'configuration': normalized})
    invalid = [{'city': 'Madrid'}, {**basic, 'email': None}, {**basic, 'id': '18446744073709551616'}, {**basic, 'id': 1}]
    for value in invalid:
        run('config', encoded(value), status=65)
    run('strict', encoded({**basic, 'unknown': 5}), status=65)
    text = run('reload', encoded(basic), '{"city":null}')
    assert text.startswith('replaced=false\n') and json.loads(text.split('\n', 1)[1]) == normalized, text
    text = run('reload', encoded(basic), encoded({**basic, 'Display-Name': 'Grace'}))
    assert text.startswith('replaced=true\n') and json.loads(text.split('\n', 1)[1])['display_name'] == 'Grace', text
    for left, right, expected in [('Display-Name', 'display_name', 'exact=false folded=true'),
                                   ('Stra\u00dfe', 'STRASSE', 'exact=false folded=false'),
                                   ('\u212a', 'k', 'exact=false folded=true'), ('x', 'x', 'exact=true folded=true')]:
        assert run('names', left, right).strip() == expected
    fragmented = '[{"name":"caf\u00e9"},"\\uD83D\\uDE00",true,null,1.25e3]'
    expected = json.loads(fragmented)
    for chunk in range(1, len(fragmented.encode()) + 2):
        lines = run('fragments', fragmented, chunk).splitlines()
        assert [json.loads(line) for line in lines if line] == expected, (chunk, lines)
    for source in ['', '{', '[1,]', '{"x":1,"x":2}', '"\\uD800"', '01', 'NaN']:
        run('compact', source, status=65)
    for args in [('fragments', '[1]', '0'), ('pretty', '{}', '9'), ('set', '{}'), ('unknown', '{}')]:
        run(*args, status=64)
    for suffix in [b'', b' binary\xff\x00tail', b' ' + b'x' * 12000]:
        text = run('first', data=b'{"id":42}' + suffix)
        lines = text.splitlines()
        assert json.loads(lines[0]) == {'id': 42}, text
        assert lines[1] == f'remaining={len(suffix)} crc32={zlib.crc32(suffix)}', text
    text = run('first', data=b'')
    assert text.startswith('end\nremaining=0 crc32=0\n'), text
    run('first', data=b'{"unfinished":', status=65)
    print(f'JSON workbench: {count} tree, schema, exact-number, fragment and reader checks passed')


if __name__ == '__main__':
    main()
