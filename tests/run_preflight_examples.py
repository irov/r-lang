#!/usr/bin/env python3
"""Exercise service configuration validation and inspect preserved error domains."""
import argparse
import subprocess
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, status=0, domain=None):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (args, result.returncode, result.stdout, result.stderr)
        if status == 0:
            if domain is None:
                assert result.stdout == 'ok\n', (args, result.stdout)
            else:
                fields = dict(x.split('=') for x in result.stdout.splitlines()[0].split())
                assert fields['domain'] == domain and int(fields['code']) >= 0 and fields['name'], (args, result.stdout)
                assert len(result.stdout.splitlines()) >= 2
        count += 1
        return result.stdout

    for text in ['', 'hello', 'caf\u00e9', '\U0001f642' * 15, 'x' * 60]:
        result = subprocess.run([exe, 'frame', text], capture_output=True, text=True, timeout=10)
        data = text.encode()
        wire = len(data).to_bytes(4, 'big') + data
        expected = f'capacity=64 used={len(wire)} crc32={zlib.crc32(wire)}\n'
        assert result.returncode == 0 and result.stdout == expected and not result.stderr
        count += 1
    for text in ['', 'hello world', 'caf\u00e9', 'x' * 60]:
        result = subprocess.run([exe, 'packet', text], capture_output=True, text=True, timeout=10)
        data = text.encode()
        expected = f'bytes={len(data)} crc32={zlib.crc32(data)}\n'
        assert result.returncode == 0 and result.stdout == expected and not result.stderr
        count += 1
    run('packet', 'x' * 61, status=64)
    run('frame', 'x' * 61, status=64)
    run('frame', '\U0001f642' * 16, status=64)

    for address in ['127.0.0.1', '::1', '2001:db8::7']:
        run('address', address)
    for address in ['', '256.0.0.1', 'host.invalid', '1.2.3']:
        run('address', address, domain='network')
    for path in ['.', '/', 'service/data']:
        run('path', path)
    for text in ['', '616263', 'c3a9', 'f09f9982']:
        run('utf8', text)
    for text in ['ff', 'c0af', 'eda080', 'e282']:
        run('utf8', text, domain='string')
    for offset in [0, 3, 5]:
        run('boundary', 'caf\u00e9', offset)
    for offset in [4, 6]:
        run('boundary', 'caf\u00e9', offset, domain='string')
    for offset in [0, 7]:
        run('bytes', offset)
    for offset in [8, 2**64-1]:
        run('bytes', offset, domain='bytes')
    run('duration', 0, 999999999)
    run('duration', 0, 1000000000, domain='time')
    run('barrier', 1)
    run('barrier', 2)
    run('barrier', 0, domain='threading')
    for capacity in [0, 1, 4096]:
        run('reserve', capacity)
    run('reserve', 2**64-1, domain='allocation')
    for text in ['0', str(2**32-1)]:
        run('integer', text)
    for text in ['', '-1', 'abc', str(2**32)]:
        output = run('integer', text, domain='conversion')
        if text == str(2**32):
            assert 'hint: choose a smaller integer' in output
    run('thread')
    run('asynchronous')
    profile = subprocess.run([exe, 'profile'], capture_output=True, text=True, timeout=10)
    assert profile.returncode == 0 and not profile.stderr
    fields = dict(item.split('=') for item in profile.stdout.strip().split())
    assert fields['profile'] == 'hosted-native-async' and fields['target']
    assert fields['execution'] == 'threads+tasks'
    count += 1
    for args in [('unknown',), ('utf8', 'x0'), ('utf8', '0'), ('barrier', 65),
                 ('reserve', 4097), ('boundary', 'x'), ('thread', 'extra')]:
        run(*args, status=64)
    run('bytes', '-1', status=65)
    print(f'Preflight: {count} configuration, resource, diagnostic and validation checks passed')


if __name__ == '__main__':
    main()
