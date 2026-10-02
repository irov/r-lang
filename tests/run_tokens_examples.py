#!/usr/bin/env python3
"""Commands of examples/tokens: identifiers of RFC 9562 (std.uuid), HMAC signatures and digests of
standard input in pieces (std.hash), bytes of the operating-system generator and draws of the
seeded generator (std.random), checked against the Python standard library and the reference
generator of tests/m20_reference.py."""
import argparse
import base64
import hashlib
import hmac
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m20_reference import Generator  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = ('tokens id v4|v7 COUNT | id parse TEXT | id at MILLISECONDS HEX20\n'
         'tokens sign KEY MESSAGE | verify KEY MESSAGE HEX | digest < INPUT\n'
         'tokens noise COUNT | pick LOW HIGH | dice SEED COUNT | shuffle SEED ITEM...\n')
UUID = re.compile(r'^[0-9a-f]{8}-[0-9a-f]{4}-([0-9a-f])[0-9a-f]{3}-([89ab])[0-9a-f]{3}-[0-9a-f]{12}$')
checks = 0


def run(values, stdin=b''):
    result = subprocess.run([args.executable, *values], input=stdin, capture_output=True,
                            timeout=60)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr='', stdin=b''):
    global checks
    outcome = run(values, stdin)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['id', 'v5', '1'], 64, '', usage)

# Fresh identifiers: format, version and variant; version 7 ones made later do not sort earlier.
for version in ('4', '7'):
    code, out, err = run(['id', 'v' + version, '5'])
    lines = out.splitlines()
    assert code == 0 and err == '' and len(lines) == 5, (code, out, err)
    for line in lines:
        match = UUID.match(line)
        assert match and match.group(1) == version, line
    assert len(set(lines)) == 5, lines
    checks += 1
code, first, _ = run(['id', 'v7', '1'])
code, second, _ = run(['id', 'v7', '1'])
assert first[:13] <= second[:13], (first, second)
checks += 1
expect(['id', 'parse', '017F22E2-79B0-7CC3-98C4-DC0C0C07398F'], 0,
       '017f22e2-79b0-7cc3-98c4-dc0c0c07398f version 7 nil=false max=false\n')
expect(['id', 'parse', '00000000-0000-0000-0000-000000000000'], 0,
       '00000000-0000-0000-0000-000000000000 version 0 nil=true max=false\n')
expect(['id', 'parse', 'FFFFFFFF-ffff-FFFF-ffff-FFFFFFFFFFFF'], 0,
       'ffffffff-ffff-ffff-ffff-ffffffffffff version 15 nil=false max=true\n')
expect(['id', 'parse', '017f22e2-79b0-7cc3-98c4'], 65, '', 'tokens: invalid_digit at byte 23\n')
expect(['id', 'parse', '017f22e2+79b0-7cc3-98c4-dc0c0c07398f'], 65, '',
       'tokens: invalid_digit at byte 8\n')
expect(['id', 'at', '0x017f22e279b0', '0cc318c4dc0c0c07398f'], 0,
       'v7 017f22e2-79b0-7cc3-98c4-dc0c0c07398f\nv4 0cc318c4-dc0c-4c07-b98f-0cc318c4dc0c\n')
expect(['id', 'at', '1', '0cc3'], 64, '', usage)

# Signatures and verification against hmac of the Python standard library.
for key, message in (('secret', 'hello world'), ('', ''), ('k' * 200, 'long key é')):
    short = hmac.new(key.encode(), message.encode(), hashlib.sha256).digest()
    long_ = hmac.new(key.encode(), message.encode(), hashlib.sha512).hexdigest()
    url = base64.urlsafe_b64encode(short).rstrip(b'=').decode()
    expect(['sign', key, message], 0,
           f'hmac-sha256 {short.hex()}\nhmac-sha256-url {url}\nhmac-sha512 {long_}\n')
    expect(['verify', key, message, short.hex()], 0, 'valid\n')
    wrong = short[:-1] + bytes([short[-1] ^ 1])
    expect(['verify', key, message, wrong.hex()], 65, 'invalid\n')
expect(['verify', 'k', 'm', 'abc'], 65, '', 'tokens: trailing_character at byte 2\n')

# Digests of standard input read in pieces.
for data in (b'', b'some input', bytes(range(256)) * 50):
    expect(['digest'], 0,
           f'{len(data)} bytes\nsha256 {hashlib.sha256(data).hexdigest()}\n'
           f'sha512 {hashlib.sha512(data).hexdigest()}\n', stdin=data)

# The operating-system generator.
code, out, err = run(['noise', '16'])
assert code == 0 and re.fullmatch(r'[0-9a-f]{32}\n', out), (code, out, err)
code, again, _ = run(['noise', '16'])
assert again != out, (out, again)
checks += 1
code, out, err = run(['pick', '10', '13'])
match = re.fullmatch(r'value (1[012])\ndie ([1-6])\nwords drawn true\n', out)
assert code == 0 and match, (code, out, err)
checks += 1
expect(['pick', '5', '5'], 64, '', usage)

# The seeded generator against the reference.
for seed, count in ((42, 10), (0, 3), (2 ** 64 - 1, 5)):
    dice = Generator(seed)
    rolls = [dice.range(1, 7) for _ in range(count)]
    word = dice.next_u64()
    half = dice.next_u32()
    tenth = dice.below(10)
    noise = dice.fill(4).hex()
    expect(['dice', str(seed), str(count)], 0,
           'rolls:' + ''.join(f' {roll}' for roll in rolls) +
           f'\nnext {word} {half} {tenth} {noise}\n')
items = ['alpha', 'beta', 'gamma', 'delta', 'epsilon']
order = Generator(7).shuffle(items)
expect(['shuffle', '7', *items], 0, ' '.join(order) + '\nkept 5\n')
expect(['shuffle', '7'], 0, '\nkept 0\n')
print(f'tokens example checks passed: {checks}')
