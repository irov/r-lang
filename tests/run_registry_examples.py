#!/usr/bin/env python3
"""Commands of examples/registry: devices kept in SQLite (std.sqlite) with a transactional outbox.
Every change of a device and its event are committed together; deliver relays committed events in
order and marks them delivered. The database files are checked with the sqlite3 module of
Python, which also writes an event of its own and holds a write lock that the program waits
for."""
import argparse
import hashlib
import json
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
executable = str(Path(args.executable).resolve())
usage = ('registry init DB | register DB ID NAME [WEIGHT] | rename DB ID NAME | retire DB ID NOTE\n'
         'registry show DB | outbox DB | deliver DB [LIMIT] | demo DB\n')
checks = 0


def run(values, cwd):
    result = subprocess.run([executable, *values], capture_output=True, timeout=120, cwd=cwd,
                            stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, cwd, status, stdout, stderr=''):
    global checks
    outcome = run(values, cwd)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


def fingerprint(device_id):
    return hashlib.sha256(device_id.encode()).digest()[:8].hex()


expect([], '.', 0, '', usage)
expect(['bogus'], '.', 64, '', usage)
expect(['register', 'x.db', 'only-id'], '.', 64, '', usage)

with tempfile.TemporaryDirectory() as directory:
    here = Path(directory)
    demo = [
        'ready (journal wal)',
        'registered sensor-1 (event 1) and sensor-2 (event 2)',
        'sensor-1 again: already_registered, nothing published',
        'renamed sensor-2 (event 3), retired sensor-1 (event 4)',
        'writer inserted 1, reader saw 2, second writer refused: true (busy), after rollback 2',
        '1 device.registered {"id":"sensor-1","name":"boiler"} (3 columns)',
        '2 device.registered {"id":"sensor-2","name":"attic"} (3 columns)',
        '3 device.renamed {"id":"sensor-2","name":"loft"} (3 columns)',
        '4 device.retired {"id":"sensor-1","name":"boiler"} (3 columns)',
        'deliver 1 device.registered {"id":"sensor-1","name":"boiler"}',
        'deliver 2 device.registered {"id":"sensor-2","name":"attic"}',
        'deliver 3 device.renamed {"id":"sensor-2","name":"loft"}',
        'delivered 3 (transaction was open: true)',
        'deliver 4 device.retired {"id":"sensor-1","name":"boiler"}',
        'delivered 1 (transaction was open: true)',
        f'sensor-1 boiler revision 2 active false weight 2.5 kg note replaced fingerprint {fingerprint("sensor-1")}',
        f'sensor-2 loft revision 2 active true weight - note - fingerprint {fingerprint("sensor-2")}',
    ]
    expect(['demo', 'demo.db'], directory, 0, '\n'.join(demo) + '\n')
    with sqlite3.connect(here / 'demo.db') as db:
        assert db.execute('PRAGMA journal_mode').fetchone() == ('wal',)
        events = db.execute('SELECT seq, topic, payload, delivered FROM outbox ORDER BY seq').fetchall()
        assert [(seq, topic, delivered) for seq, topic, _, delivered in events] == [
            (1, 'device.registered', 1), (2, 'device.registered', 1), (3, 'device.renamed', 1),
            (4, 'device.retired', 1)], events
        assert json.loads(events[2][2]) == {'id': 'sensor-2', 'name': 'loft'}
        devices = db.execute('SELECT id, typeof(weight), typeof(note), hex(fingerprint) FROM device '
                             'ORDER BY id').fetchall()
        assert devices == [('sensor-1', 'real', 'text', fingerprint('sensor-1').upper()),
                           ('sensor-2', 'null', 'null', fingerprint('sensor-2').upper())], devices
    checks += 1
    # The emptied write-ahead log and its index stay, so read-only commands open the file.
    assert (here / 'demo.db-wal').exists() and (here / 'demo.db-shm').exists()

    expect(['init', 'r.db'], directory, 0, 'ready (journal wal)\n')
    expect(['register', 'r.db', 'pump-1', 'cellar', '12.5'], directory, 0, 'registered, event 1\n')
    expect(['register', 'r.db', 'pump-2', 'garden'], directory, 0, 'registered, event 2\n')
    # A known identifier rolls back the device and its event together.
    expect(['register', 'r.db', 'pump-1', 'again'], directory, 65, '', 'refused: already_registered\n')
    expect(['rename', 'r.db', 'pump-9', 'nowhere'], directory, 65, '', 'refused: unknown_device\n')
    expect(['rename', 'r.db', 'pump-2', 'orchard'], directory, 0, 'renamed, event 3\n')
    expect(['register', 'r.db', 'pump-3', 'pond', 'heavy'], directory, 64, '', usage)
    # Python writes a device and its event in one transaction of its own.
    with sqlite3.connect(here / 'r.db') as db:
        db.execute("INSERT INTO device VALUES ('valve-1', 'python', 1, 1, NULL, 'from python', x'00ff')")
        db.execute("INSERT INTO outbox(topic, payload) VALUES ('device.registered', '{\"id\":\"valve-1\"}')")
    expect(['outbox', 'r.db'], directory, 0,
           '1 device.registered {"id":"pump-1","name":"cellar"} (3 columns)\n'
           '2 device.registered {"id":"pump-2","name":"garden"} (3 columns)\n'
           '3 device.renamed {"id":"pump-2","name":"orchard"} (3 columns)\n'
           '4 device.registered {"id":"valve-1"} (3 columns)\n')
    expect(['deliver', 'r.db', '2'], directory, 0,
           'deliver 1 device.registered {"id":"pump-1","name":"cellar"}\n'
           'deliver 2 device.registered {"id":"pump-2","name":"garden"}\n'
           'delivered 2 (transaction was open: true)\n')
    # A writer of Python holds the lock for 300 ms; the program waits for it (busy timeout 2 s).
    holder = sqlite3.connect(here / 'r.db', isolation_level=None, check_same_thread=False)
    holder.execute('BEGIN IMMEDIATE')
    holder.execute("UPDATE device SET note = 'held' WHERE id = 'valve-1'")
    release = threading.Timer(0.3, lambda: holder.execute('COMMIT'))
    release.start()
    started = time.monotonic()
    expect(['retire', 'r.db', 'pump-1', 'worn out'], directory, 0, 'retired, event 5\n')
    assert time.monotonic() - started >= 0.25
    release.join()
    holder.close()
    expect(['deliver', 'r.db'], directory, 0,
           'deliver 3 device.renamed {"id":"pump-2","name":"orchard"}\n'
           'deliver 4 device.registered {"id":"valve-1"}\n'
           'deliver 5 device.retired {"id":"pump-1","name":"cellar"}\n'
           'delivered 3 (transaction was open: true)\n')
    expect(['deliver', 'r.db'], directory, 0, 'delivered 0 (transaction was open: true)\n')
    expect(['show', 'r.db'], directory, 0,
           f'pump-1 cellar revision 2 active false weight 12.5 kg note worn out fingerprint {fingerprint("pump-1")}\n'
           f'pump-2 orchard revision 2 active true weight - note - fingerprint {fingerprint("pump-2")}\n'
           'valve-1 python revision 1 active true weight - note held fingerprint 00ff\n')
    with sqlite3.connect(here / 'r.db') as db:
        assert db.execute('SELECT count(*), sum(delivered) FROM outbox').fetchone() == (5, 5)
    checks += 1
    expect(['show', 'missing.db'], directory, 66, '', 'sqlite: cannot_open (14) unable to open database file\n')
    # A write lock held past the busy timeout fails the change, which leaves nothing behind.
    holder = sqlite3.connect(here / 'r.db', isolation_level=None)
    holder.execute('BEGIN EXCLUSIVE')
    expect(['register', 'r.db', 'pump-4', 'late'], directory, 70, '', 'sqlite: busy (5) database is locked\n')
    holder.execute('ROLLBACK')
    holder.close()
    with sqlite3.connect(here / 'r.db') as db:
        assert db.execute("SELECT count(*) FROM device WHERE id = 'pump-4'").fetchone() == (0,)
    checks += 1

print(f'registry examples: {checks} checks passed')
