#!/usr/bin/env python3
"""Commands of examples/journal: fixed-size records in a data file with a write-ahead journal
(std.fs locks, positional reads and writes, sync levels and memory mappings; Library
R-SLIB-FS-0014..0017). The files are checked byte by byte here; a lock that a journal process
holds is seen by fcntl.lockf of Python, an update waits for it or times out, a scan reads the
records through a mapping, and an update interrupted after its journal is replayed by recover
through a writable mapping, while a torn journal is not."""
import argparse
import fcntl
import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
executable = str(Path(args.executable).resolve())
usage = ('journal init DIR RECORDS | put DIR SLOT TEXT [WAIT_MS] | get DIR SLOT | scan DIR\n'
         'journal crash DIR SLOT TEXT | recover DIR | hold DIR MS | busy DIR | demo DIR\n')
RECORD, HEADER = 64, 16
checks = 0


def run(values):
    result = subprocess.run([executable, *values], capture_output=True, timeout=120,
                            stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr=''):
    global checks
    outcome = run(values)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


def record(text):
    data = text.encode()[:62]
    return bytes([1, len(data)]) + data + bytes(RECORD - 2 - len(data))


def slot_bytes(directory, slot):
    return (directory / 'store.dat').read_bytes()[slot * RECORD:(slot + 1) * RECORD]


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['put', 'x', '1'], 64, '', usage)

with tempfile.TemporaryDirectory() as name:
    expect(['demo', name], 0,
           'store of 4 records\n'
           'slot 0: alpha, slot 1: beta\n'
           'another open file holds a shared lock of slot 0, a probe finds the store busy: true\n'
           'an update waited for that lock and then wrote slot 2: delta\n'
           'interrupted after the journal of slot 1, which still reads beta\n'
           'replayed slot 1, slot 1 now reads gamma\n'
           'a second recovery: nothing to replay; busy: false\n')

with tempfile.TemporaryDirectory() as name:
    here = Path(name)
    expect(['init', name, '8'], 0, 'store of 8 records\n')
    assert (here / 'store.dat').read_bytes() == bytes(8 * RECORD)
    assert (here / 'store.journal').read_bytes() == bytes(HEADER)
    expect(['put', name, '3', 'hello'], 0, 'slot 3: hello\n')
    assert slot_bytes(here, 3) == record('hello')
    assert (here / 'store.journal').read_bytes()[:HEADER] == bytes(HEADER)
    expect(['get', name, '3'], 0, 'hello\n')
    expect(['get', name, '0'], 0, '(empty)\n')
    # A scan maps the data file: the records it reads are the bytes Python wrote there too.
    expect(['scan', name], 0, 'slot 3: hello\n')
    with open(here / 'store.dat', 'r+b') as file:
        file.seek(7 * RECORD)
        file.write(record('written by python'))
    expect(['scan', name], 0, 'slot 3: hello\nslot 7: written by python\n')
    expect(['scan', name, 'x'], 64, '', usage)
    expect(['get', name, '9'], 65, '', 'no slot 9 in the store\n')

    # Another process holds the exclusive lock: Python's POSIX lock and a probe see it, an
    # update waits for it, a short wait times out. The lock is held long enough that the probes
    # and the short wait all start before it ends even in slow sanitizer builds (M34-1).
    holder = subprocess.Popen([executable, 'hold', name, '6000'], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, stdin=subprocess.DEVNULL)
    assert holder.stdout.readline() == b'held\n'
    held_at = time.monotonic()
    with open(here / 'store.dat', 'r+b') as file:
        try:
            fcntl.lockf(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
            raise AssertionError('Python took the lock that the journal process holds')
        except OSError:
            pass
    expect(['busy', name], 0, 'busy\n')
    expect(['put', name, '4', 'late', '200'], 75, '', 'file system: timed_out\n')
    assert time.monotonic() - held_at < 6.0, 'the short wait started after the lock was released'
    expect(['put', name, '4', 'waited', '30000'], 0, 'slot 4: waited\n')
    assert time.monotonic() - held_at >= 1.0, 'the update did not wait for the lock'
    assert holder.wait(timeout=30) == 0 and holder.stdout.read() == b'released\n'
    checks += 1
    expect(['busy', name], 0, 'free\n')
    with open(here / 'store.dat', 'r+b') as file:
        fcntl.lockf(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fcntl.lockf(file, fcntl.LOCK_UN)
    assert slot_bytes(here, 4) == record('waited')

    # An update that dies after its journal leaves a complete entry that recover replays.
    expect(['crash', name, '5', 'crashed'], 0, 'journal of slot 5 written, data not written\n')
    journal = (here / 'store.journal').read_bytes()
    magic, slot, length, checksum = struct.unpack('<4sIII', journal[:HEADER])
    assert (magic, slot, length) == (b'RJ1\0', 5, RECORD)
    assert journal[HEADER:HEADER + RECORD] == record('crashed')
    assert checksum == zlib.crc32(record('crashed'))
    assert slot_bytes(here, 5) == bytes(RECORD)
    expect(['get', name, '5'], 0, '(empty)\n')
    expect(['recover', name], 0, 'replayed slot 5\n')
    expect(['get', name, '5'], 0, 'crashed\n')
    assert slot_bytes(here, 5) == record('crashed')
    assert (here / 'store.dat').stat().st_size == 8 * RECORD
    expect(['recover', name], 0, 'nothing to replay\n')

    # A torn journal entry, whose record does not match its checksum, is not replayed.
    expect(['crash', name, '6', 'torn'], 0, 'journal of slot 6 written, data not written\n')
    journal = bytearray((here / 'store.journal').read_bytes())
    journal[HEADER + 5] ^= 0xFF
    (here / 'store.journal').write_bytes(bytes(journal))
    expect(['recover', name], 0, 'nothing to replay\n')
    expect(['get', name, '6'], 0, '(empty)\n')

with tempfile.TemporaryDirectory() as name:
    expect(['init', name, '0'], 0, 'store of 0 records\n')
    expect(['scan', name], 0, 'no records\n')

print(f'journal examples: {checks} checks passed')
