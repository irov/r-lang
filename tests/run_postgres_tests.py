#!/usr/bin/env python3
"""Runs the tests of std.postgres (library/r/tests/postgres.r translated in test mode, Library
R-SLIB-PG-0001..0012) against a temporary PostgreSQL cluster: SCRAM-SHA-256, md5 and cleartext
password users over TCP, trust over the Unix-domain socket, and TLS with the certificate of
tests/fixtures/tls."""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from postgres_cluster import cluster  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
parser.add_argument('--bin', required=True, help='directory of initdb, pg_ctl and psql')
args = parser.parse_args()
fixtures = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures', 'tls')

with cluster(args.bin, fixtures) as server:
    version = server.psql('SHOW server_version').strip()
    environment = dict(os.environ)
    environment.update(server.environment())
    environment.update({'R_POSTGRES_TEST': '1', 'R_POSTGRES_SOCKET': server.socket_dir,
                        'R_POSTGRES_TLS_AUTHORITY': os.path.join(fixtures, 'authority.pem')})
    # The program gets less time than CTest gives this driver, so that a hang still ends here and
    # the cluster is stopped.
    try:
        result = subprocess.run([os.path.abspath(args.executable)], env=environment, capture_output=True,
                                text=True, timeout=300, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired as expired:
        sys.stdout.write((expired.stdout or b'').decode() if isinstance(expired.stdout, bytes) else (expired.stdout or ''))
        sys.exit('the std.postgres tests did not finish within 300 seconds')
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        sys.exit(f'the std.postgres tests failed with status {result.returncode}')
print(f'std.postgres tests passed against PostgreSQL {version}')
