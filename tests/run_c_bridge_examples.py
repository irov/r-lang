#!/usr/bin/env python3
"""Exercise libc, bounded C strings, runtime attachment, both handle cleanup paths and a library
opened at run time."""
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, expected=None, status=0):
        nonlocal count
        result = subprocess.run([exe, *args], capture_output=True, text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (args[:2], result.returncode, result.stdout, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args[:2], result.stdout, expected)
        count += 1
        return result.stdout

    target = dict(item.split('=') for item in run('target').split())
    assert target['pointer_bits'] == '64' and target['libc'] == 'true', target
    for field in ['c_wint', 'long_double', 'async']:
        assert target[field] in ['true', 'false'], target
    for text in ['', 'Hello, C', 'aZ09_!', 'caf\u00e9 \U0001f642', 'x' * 256, 'a\nb\tc']:
        length = len(text.encode())
        expected = f'storage={length + 1} bytes={length} strlen={length} text={text}\n'
        run('inspect', text, expected=expected)
        run('cstring', text.encode().hex(), expected=expected)
        transformed = text.translate(str.maketrans('abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'))
        for command in ['upper', 'release']:
            run(command, text, expected=transformed + '\n')
    for raw in [b'\x00', b'hello\x00', b'hello\x00\xff', b'\x00\xff',
                '\u00e9\U0001f642'.encode() + b'\x00junk']:
        prefix = raw.split(b'\x00', 1)[0]
        run('bytes', raw.hex(), expected=f'bytes={len(prefix)} text={prefix.decode()}\n')
    for raw in [b'', b'abc', b'\xff\x00', b'\xc0\xaf\x00', b'\xed\xa0\x80\x00', b'\xe2\x82\x00']:
        run('bytes', raw.hex(), status=65)
    for raw in [b'a\x00b', b'\x00', b'\xff']:
        run('cstring', raw.hex(), status=65)
    run('attachment', expected='attached and detached; duplicate=resource_exhausted\n')
    # R-FFI-0063: the system library opened at run time by name and by path; labs is resolved
    # with the prototype <stdlib.h> verifies. A refused library is unavailable, an empty path a
    # usage error.
    for library in ['libSystem.B.dylib', '/usr/lib/libSystem.B.dylib']:
        run('loadable', library, expected='labs(-42)=42 strlen=true\n')
    run('loadable', '/nonexistent/libnothing.dylib', expected='library not loaded\n', status=69)
    run('loadable', '', expected='invalid library path\n', status=64)
    for command in ['upper', 'release']:
        run(command, 'x' * 257, status=64)
        run(command, '\u00e9' * 129, status=64)
    for args in [('target', 'extra'), ('inspect',), ('unknown',), ('bytes', 'x0'), ('bytes', '0'), ('cstring', 'zz')]:
        run(*args, status=64)
    print(f'C bridge: {count} libc, UTF-8, termination, callback, handle, loading and validation checks passed')


if __name__ == '__main__':
    main()
