#!/usr/bin/env python3
"""Commands of examples/ingest: messages of "key=value;key=value" fields parsed by worker tasks
into arenas lent by a pool (std.arena, std.pool), each batch in a budget whose usage it reports
(std.alloc::budget_usage). The reports are checked against a model of the parser and the arena
written here: the fields, the rejected lines and their reasons, the first message and the bytes
of the arena; the budget lines are checked for consistency, and a budget too small for one block
refuses every batch."""
import argparse
import random
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
executable = str(Path(args.executable).resolve())
usage = 'ingest demo | run BATCH_LINES BUDGET_BYTES < MESSAGES\n'
BLOCK, LIMIT = 256, 1024
checks = 0


def run(values, text=''):
    result = subprocess.run([executable, *values], input=text.encode(), capture_output=True,
                            timeout=120)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr='', text=''):
    global checks
    outcome = run(values, text)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


class TooLarge(Exception):
    pass


class Arena:
    """Blocks of BLOCK bytes, a larger block for a larger piece, LIMIT bytes in all."""

    def __init__(self):
        self.blocks = []
        self.reserved = 0
        self.used = 0

    def store(self, data):
        if not self.blocks or self.blocks[-1][0] - self.blocks[-1][1] < len(data):
            size = max(BLOCK, len(data))
            if size > LIMIT or self.reserved > LIMIT - size:
                raise TooLarge()
            self.blocks.append([size, 0])
            self.reserved += size
        self.blocks[-1][1] += len(data)
        self.used += len(data)
        return data


def parse(arena, line):
    raw = line.encode()
    fields = []
    start = 0
    for index in range(len(raw) + 1):
        if index == len(raw) or raw[index] == ord(';'):
            if index > start:
                part = raw[start:index]
                equals = part.find(b'=')
                if equals < 0:
                    return None, ('missing_equals', start)
                if equals == 0:
                    return None, ('empty_key', start)
                try:
                    key = arena.store(part[:equals])
                    value = arena.store(part[equals + 1:])
                except TooLarge:
                    return None, ('too_large', start)
                fields.append((key, value))
            start = index + 1
    return fields, None


def report(lines, first_line, budget):
    """The report of a batch as the program prints it, but for the budget line, which is
    returned as a pattern."""
    arena = Arena()
    messages, rejected = [], []
    for offset, line in enumerate(lines):
        fields, problem = parse(arena, line)
        if problem is None:
            messages.append(fields)
        else:
            rejected.append((first_line + offset, *problem))
    last = first_line + len(lines) - 1
    count = sum(len(fields) for fields in messages)
    text = f'lines {first_line}-{last}: {len(messages)} messages, {count} fields, {len(rejected)} rejected\n'
    if messages:
        sample = ' '.join(f'{k.decode()}={v.decode()}' for k, v in messages[0])
        text += f'  first message: {sample}\n'
    for line, reason, position in rejected:
        text += f'  line {line} rejected: {reason} at byte {position}\n'
    text += f'  arena: {arena.used} bytes stored in {arena.reserved} reserved\n'
    return text, len(messages), len(rejected)


BUDGET_LINE = re.compile(r'  budget of (\d+) bytes: (\d+) charged, (\d+) left\n')


def check_run(lines, batch_lines, budget):
    """Runs the lines in batches and compares every report with the model."""
    global checks
    status, out, err = run(['run', str(batch_lines), str(budget)], ''.join(l + '\n' for l in lines))
    assert (status, err) == (0, ''), (status, err)
    position = 0
    totals = [0, 0, 0]
    for start in range(0, len(lines), batch_lines):
        part = lines[start:start + batch_lines]
        text, messages, rejected = report(part, start + 1, budget)
        assert out.startswith(text, position), (out[position:position + 400], text)
        position += len(text)
        match = BUDGET_LINE.match(out, position)
        assert match is not None, out[position:position + 200]
        stated, charged, left = (int(group) for group in match.groups())
        assert stated == budget and charged + left == budget and charged > 0, match.group(0)
        position = match.end()
        totals[0] += 1
        totals[1] += messages
        totals[2] += rejected
    tail = (f'total: batches {totals[0]}, messages {totals[1]}, rejected lines {totals[2]}, '
            f'refused batches 0, arenas 2\n')
    assert out[position:] == tail, (out[position:], tail)
    checks += 1


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['demo', 'extra'], 64, '', usage)
expect(['run', '2'], 64, '', usage)
expect(['run', '0', '100'], 64, '', usage)
expect(['run', 'two', '100'], 64, '', usage)

demo = '''pool: 2 arenas of 256-byte blocks, at most 1024 bytes each, 2 available
two leases: 0 available, a third acquire finds none: true
one lease dropped: 1 available
lines 1-4: 3 messages, 9 fields, 1 rejected
  first message: device=boiler metric=temperature value=21.5
  line 3 rejected: missing_equals at byte 0
  arena: 108 bytes stored in 256 reserved
  budget of 8192 bytes: 1440 charged, 6752 left
lines 5-8: 2 messages, 7 fields, 2 rejected
  first message: device=garage metric=temperature value=8.5
  line 6 rejected: empty_key at byte 14
  line 7 rejected: too_large at byte 14
  arena: 103 bytes stored in 256 reserved
  budget of 8192 bytes: 1248 charged, 6944 left
lines 9-11: the budget of 1000 bytes refused the batch (budget_exhausted) after 1 of 3 lines
after the batches: 2 available; outside every budget block budget_usage() is none: true
'''
expect(['demo'], 0, demo)

# No input: no batch.
expect(['run', '3', '4096'], 0,
       'total: batches 0, messages 0, rejected lines 0, refused batches 0, arenas 2\n')

# A budget smaller than one block of the arena refuses every batch before its first message.
valid = [f'device=d{index};value={index}' for index in range(5)]
expect(['run', '2', '100'], 0,
       'lines 1-2: the budget of 100 bytes refused the batch (budget_exhausted) after 0 of 2 lines\n'
       'lines 3-4: the budget of 100 bytes refused the batch (budget_exhausted) after 0 of 2 lines\n'
       'lines 5-5: the budget of 100 bytes refused the batch (budget_exhausted) after 0 of 1 lines\n'
       'total: batches 3, messages 0, rejected lines 0, refused batches 3, arenas 2\n',
       text=''.join(line + '\n' for line in valid))

# Hand-written lines: empty parts, an empty value, an empty line, text that is not ASCII, a key
# without '=' after a stored part, an empty key, and parts larger than a block and than the
# arena.
lines = [
    'a=1;;b=2;',
    'empty=',
    '',
    'город=Київ;café=crème',
    'k=v;broken',
    'k=v;=v',
    'big=' + 'y' * 300,
    'huge=' + 'z' * 1100,
    'after=huge',
]
check_run(lines, 4, 65536)
check_run(lines, 1, 65536)
check_run(lines, 9, 65536)

# Random messages against the model, in batches of several sizes.
generator = random.Random(31)
alphabet = 'abcdefghij=;é'
lines = []
for _ in range(240):
    kind = generator.random()
    if kind < 0.7:
        count = generator.randint(1, 6)
        parts = [f'{generator.choice("kmnp")}{generator.randint(0, 99)}='
                 + ''.join(generator.choice('abcdefghijé') for _ in range(generator.randint(0, 40)))
                 for _ in range(count)]
        lines.append(';'.join(parts))
    elif kind < 0.9:
        lines.append(''.join(generator.choice(alphabet) for _ in range(generator.randint(0, 30))))
    else:
        lines.append('blob=' + 'q' * generator.randint(200, 1200))
for batch_lines in (1, 5, 13, 240):
    check_run(lines, batch_lines, 1 << 20)

print(f'ingest examples: {checks} checks passed')
