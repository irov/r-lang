#!/usr/bin/env python3
"""Commands of examples/orders: products and orders in PostgreSQL (std.postgres) against a
temporary cluster. Orders take stock in a transaction and are announced with NOTIFY; products and
orders move as CSV with COPY. psql checks the tables, and a watch process hears the orders that
other processes place."""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from postgres_cluster import cluster  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
parser.add_argument('--bin', required=True, help='directory of initdb, pg_ctl and psql')
args = parser.parse_args()
executable = str(Path(args.executable).resolve())
usage = ('orders init | stock SKU NAME COUNT | place SKU QUANTITY | show | totals | dump | load FILE\n'
         'orders watch COUNT | demo\n')
fixtures = Path(__file__).resolve().parent / 'fixtures' / 'tls'
checks = 0
environment = dict(os.environ)


def run(values, extra=None):
    env = dict(environment)
    env.update(extra or {})
    result = subprocess.run([executable, *values], capture_output=True, timeout=120, env=env,
                            stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr='', extra=None):
    global checks
    outcome = run(values, extra)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['place', 'tea'], 64, '', usage)

with cluster(args.bin, str(fixtures)) as server:
    environment.update(server.environment())
    expect(['init'], 0, 'ready\n')
    expect(['stock', 'tea', 'green tea', '5'], 0, 'stock tea: 5\n')
    expect(['place', 'tea', '2'], 0, 'order 1: 3 left\n')
    expect(['place', 'tea', '9'], 65, 'refused: only 3 of tea\n')
    expect(['place', 'nope', '1'], 65, 'refused: no product nope\n')
    assert server.psql('SELECT stock FROM products WHERE sku = \'tea\'').strip() == '3'
    expect(['show'], 0, '1 green tea x2 (3 left)\n')
    expect(['dump'], 0, '1,tea,2\n')
    with tempfile.TemporaryDirectory() as folder:
        products = Path(folder) / 'products.csv'
        products.write_text('coffee,"coffee, dark",7\nwater,water,12\n')
        expect(['load', str(products)], 0, 'imported 2\n')
        products.write_text('broken,line\n')
        status, out, err = run(['load', str(products)])
        assert status == 69 and out == '' and err.startswith('postgres: 22P04 '), (status, out, err)
        checks += 1
    assert server.psql('SELECT name, stock FROM products ORDER BY sku') == \
        'coffee, dark|7\ntea|3\nwater|12\n'.replace('tea|3', 'green tea|3')

    # A watch process hears the orders that other processes place.
    watcher = subprocess.Popen([executable, 'watch', '2'], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               env=environment, stdin=subprocess.DEVNULL)
    assert watcher.stdout.readline() == b'listening\n'
    expect(['place', 'coffee', '3'], 0, 'order 2: 4 left\n')
    expect(['place', 'water', '1'], 0, 'order 3: 11 left\n')
    assert watcher.wait(timeout=60) == 0
    assert watcher.stdout.read() == b'order 2 coffee 3\norder 3 water 1\n'
    checks += 1

    expect(['totals'], 0, 'sku ordered\ncoffee 3\ntea 2\nwater 1\n')

    # With PGSSLROOTCERT the program connects over TLS and checks the certificate of the server.
    expect(['show'], 0, '1 green tea x2 (3 left)\n2 coffee, dark x3 (4 left)\n3 water x1 (11 left)\n',
           extra={'PGHOST': 'localhost', 'PGSSLROOTCERT': str(fixtures / 'authority.pem')})
    assert 'ssl' in server.psql("SHOW ssl").strip() or server.psql("SHOW ssl").strip() == 'on'
    status, out, err = run(['show'], {'PGHOST': 'localhost', 'PGSSLROOTCERT': str(fixtures / 'other_authority.pem')})
    assert status == 69 and err == 'postgres: connection TLS failed: untrusted_certificate\n', (status, out, err)
    checks += 1
    # A server that is not there is a failure of the transport.
    status, out, err = run(['show'], {'PGPORT': '1'})
    assert status == 71 and out == '' and err.startswith('fault: '), (status, out, err)
    checks += 1

    # A wrong password and a missing database are refused by the server.
    status, out, err = run(['show'], {'PGPASSWORD': 'wrong'})
    assert status == 69 and err.startswith('postgres: 28P01 '), (status, out, err)
    checks += 1
    status, out, err = run(['show'], {'PGDATABASE': 'missing'})
    assert status == 69 and err.startswith('postgres: 3D000 '), (status, out, err)
    checks += 1

    # The same program over the Unix-domain socket, where the server trusts the user.
    expect(['show'], 0, '1 green tea x2 (3 left)\n2 coffee, dark x3 (4 left)\n3 water x1 (11 left)\n',
           extra={'PGHOST': server.socket_dir, 'PGPASSWORD': ''})

    expect(['demo'], 0,
           'stock: green tea 5\n'
           'two tasks ordered 2 each: 2 accepted\n'
           'refused: only 1 of tea\n'
           'announced on channel orders: 2\n'
           'long statements cancelled: 1\n'
           '1,tea,2\n'
           '2,tea,2\n'
           '1 green tea x2 (1 left)\n'
           '2 green tea x2 (1 left)\n')
    assert server.psql('SELECT count(*) FROM orders').strip() == '2'

print(f'orders examples: {checks} checks passed')
