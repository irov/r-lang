#!/usr/bin/env python3
"""Buffered line protocols of examples/relay: standard input, record files, a reader chosen when
the program runs, a TCP echo and a /bin/cat pipe (std.stream, std.bufio, std.console)."""
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = 'relay number|ask\nrelay fields FILE\nrelay count [FILE]\nrelay echo|pipe WORD...\n'


def run(values, stdin=b''):
    result = subprocess.run([args.executable, *values], input=stdin, capture_output=True,
                            timeout=60)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


checks = 0


def expect(values, stdin, status, stdout, stderr=''):
    global checks
    outcome = run(values, stdin)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


expect([], b'', 0, '', usage)
expect(['bogus'], b'', 64, '', usage)
numbered = '   1  alpha\n   2  beta\n   3  \n   4  gamma\n----\n'
expect(['number'], b'alpha\r\nbeta\n\ngamma', 0,
       numbered + '4 lines, 47 bytes left for the final flush\n')
expect(['number'], b'', 65, '----\n0 lines, 5 bytes left for the final flush\n')
# A line longer than the reader's 256-byte buffer reaches the main error boundary.
code, out, err = run(['number'], b'x' * 300 + b'\n')
assert code == 113 and 'resource_exhausted' in err, (code, out, err)
checks += 1
with tempfile.TemporaryDirectory(prefix='r-relay-') as directory:
    records = os.path.join(directory, 'records.bin')
    with open(records, 'wb') as file:
        file.write(b'RLY1one;two;;three')
    expect(['fields', records], b'', 0,
           'field 1: one\nfield 2: two\nfield 3: \nfield 4: three\n')
    other = os.path.join(directory, 'other.bin')
    with open(other, 'wb') as file:
        file.write(b'XXXXone;')
    expect(['fields', other], b'', 65, '', 'relay: not a record file\n')
    short = os.path.join(directory, 'short.bin')
    with open(short, 'wb') as file:
        file.write(b'RL')
    code, out, err = run(['fields', short])
    assert code == 117 and 'unexpected_end' in err, (code, out, err)
    checks += 1
    expect(['fields'], b'', 64, '')
    expect(['count', records], b'', 0, 'bytes=18 lines=0\n')
    code, out, err = run(['count', os.path.join(directory, 'missing')])
    assert code == 113 and 'not_found' in err, (code, out, err)
    checks += 1
expect(['count'], b'a\nbb\nccc\n', 0, 'bytes=9 lines=3\n')
expect(['count'], b'', 0, 'bytes=0 lines=0\n')
expect(['echo', 'ping', 'hello world', ''], b'', 0,
       'echo: 4 ping\necho: 11 hello world\necho: 0 \n3 lines served\n')
expect(['echo'], b'', 64, '')
expect(['pipe', 'red', 'green'], b'', 0, 'cat: red\ncat: green\ncat exited with 0\n')
expect(['pipe'], b'', 64, '')
expect(['ask'], b'Ada\nrest', 0, 'name (up to 65536 bytes)? hello, Ada\n')
expect(['ask'], b'', 65, 'name (up to 65536 bytes)? ', 'relay: no name given\n')
print(f'Relay: {checks} buffered stream checks passed')
