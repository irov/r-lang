#!/usr/bin/env python3
"""Commands of examples/textkit: fields, lines, scalars and case folding (std.text), encodings
(std.encoding), string edits and packed records (R parts of std.string and std.bytes), number
statistics (std.iter under `I::Item:` constraints) and regex groups (std.regex captures)."""
import argparse
import base64
import subprocess
import urllib.parse

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = ('textkit fields LINE SEPARATOR | lines TEXT | scalars TEXT | fold TEXT\n'
         'textkit join SEPARATOR PART... | replace TEXT PATTERN REPLACEMENT\n'
         'textkit encode TEXT | decode base64|base64url|hex|percent TEXT\n'
         'textkit edit TEXT START END REPLACEMENT | insert TEXT AT INSERTION\n'
         'textkit pack NUMBER... | stats NUMBER... | groups PATTERN TEXT [OFFSET]\n')
checks = 0


def expect(values, status, stdout, stderr=''):
    global checks
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=60)
    outcome = (result.returncode, result.stdout.decode(), result.stderr.decode())
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['fields', 'only'], 64, '', usage)

# std.text: pieces with their surrounding whitespace, lines, scalars and case.
expect(['fields', 'a, b ,, c', ','], 0,
       'field 1: [a] lead=0 trail=0\nfield 2: [b] lead=1 trail=1\n'
       'field 3: [] lead=0 trail=0\nfield 4: [c] lead=1 trail=0\n')
expect(['fields', 'k=v', ''], 0, 'field 1: [k=v] lead=0 trail=0\n')
expect(['lines', 'one\r\ntwo\n\nthree\n'], 0, '  1| one\n  2| two\n  3| \n  4| three\n4 lines\n')
expect(['lines', ''], 0, '0 lines\n')
expect(['scalars', 'aé€😀'], 0,
       '  0  U+0061\n  1  U+00e9\n  3  U+20ac\n  6  U+1f600\n'
       '10 bytes; byte 5: boundary=false previous=3 next=6\n')
expect(['scalars', 'ab'], 0, '  0  U+0061\n  1  U+0062\n2 bytes; byte 1: boundary=true previous=0 next=2\n')
expect(['fold', 'Hello, Wörld'], 0, 'upper: HELLO, WöRLD\nlower: hello, wörld\nfirst byte: 72\n')
expect(['fold', 'é'], 0, 'upper: é\nlower: é\nfirst byte: 195\n')
expect(['join', '-', 'x', 'y', 'z'], 0, 'x-y-z\n')
expect(['join', ', '], 0, '\n')
expect(['replace', 'aaa', 'aa', 'b'], 0, 'ba\n')
expect(['replace', 'a.b.c', '.', '::'], 0, 'a::b::c\n')

# std.encoding against the Python standard library.
for text in ('hi é?', '', 'fooba', '~a b/'):
    data = text.encode()
    expect(['encode', text], 0,
           f'base64: {base64.b64encode(data).decode()}\n'
           f'base64url: {base64.urlsafe_b64encode(data).rstrip(b"=").decode()}\n'
           f'hex: {data.hex()}\npercent: {urllib.parse.quote(text, safe="")}\n')
expect(['decode', 'base64', 'aGkgw6k/'], 0, '6 bytes: hi é?\n')
expect(['decode', 'base64url', 'aGkgw6k_'], 0, '6 bytes: hi é?\n')
expect(['decode', 'base64', 'aGkgw6k_'], 65, '', 'textkit: invalid_digit at byte 7\n')
expect(['decode', 'base64', 'Zm9vY'], 65, '', 'textkit: trailing_character at byte 4\n')
expect(['decode', 'hex', '00ff'], 0, '2 bytes: 0x00ff\n')
expect(['decode', 'hex', 'abc'], 65, '', 'textkit: trailing_character at byte 2\n')
expect(['decode', 'percent', 'a%20b%C3%A9'], 0, '5 bytes: a bé\n')
expect(['decode', 'percent', 'a%zz'], 65, '', 'textkit: invalid_digit at byte 1\n')

# The R parts of std.string and std.bytes.
expect(['edit', 'héllo', '1', '3', 'E'], 0, 'hEllo\n')
expect(['edit', 'héllo', '2', '3', 'E'], 65, '', 'textkit: not_scalar_boundary\n')
expect(['edit', 'abc', '2', '9', 'x'], 65, '', 'textkit: out_of_bounds\n')
expect(['edit', 'abc', 'x', '1', 'x'], 64, '', usage)
expect(['insert', 'abc', '3', '!'], 0, 'abc!\n')
expect(['insert', 'abc', '0', 'é'], 0, 'éabc\n')
expect(['pack', '7', '300', '70000', '4294967296', '1'], 0,
       '16 bytes: 07012c70110100000000010000000001\n'
       'read 7\nread 300\nread 70000\nread 4294967296\nread 1\n')
expect(['pack', '0x1234', '0x1234'], 0, '4 bytes: 34121234\nread 4660\nread 4660\n')
expect(['pack', '4294967296', '4294967296', '4294967296', '4294967296', '1'], 65, '',
       'textkit: the record holds 32 bytes\n')
expect(['pack', '-1'], 64, '', usage)

# std.iter reductions and adapters, and a header constraint on the items of an iterator.
expect(['stats', '5', '-1', '4', '-1', '5', '9', '2'], 0,
       'sum 23 even 2 greatest 1x min -1 max 9\nreversed: 2 9 5 -1 4 -1 5\n'
       'chunks: 8 13 2\nsteps: -6 5 -5 6 4 -7\n')
expect(['stats', '3', '3', '1'], 0,
       'sum 7 even 0 greatest 2x min 1 max 3\nreversed: 1 3 3\nchunks: 7\nsteps: 0 -2\n')
expect(['stats'], 0, 'sum 0 even 0 greatest 0x min - max -\nreversed:\nchunks:\nsteps:\n')

# std.regex captures.
expect(['groups', r'(\d+)-(\d+)(?:-(\d+))?', 'on 2026-09 ok'], 0,
       '3 groups\n0: 3..10 [2026-09]\n1: 3..7 [2026]\n2: 8..10 [09]\n3: none\n')
expect(['groups', '(a|b)+', 'xxabba', '1'], 0, '1 groups\n0: 2..6 [abba]\n1: 5..6 [a]\n')
expect(['groups', '(a)|(ab)', 'ab'], 0, '2 groups\n0: 0..2 [ab]\n1: none\n2: 0..2 [ab]\n')
expect(['groups', 'x(y)', 'abc'], 0, '1 groups\nno match\n')
expect(['groups', 'a', 'é', '1'], 65, '', 'textkit: invalid_offset at byte 1\n')
print(f'textkit example checks passed: {checks}')
