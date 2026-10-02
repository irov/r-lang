"""A temporary PostgreSQL cluster for the tests of std.postgres and of its example.

The cluster lives in a fresh temporary directory, which also holds its Unix-domain socket (a
short path, as macOS limits socket paths to 103 bytes). Over TCP on localhost the user postgres
authenticates with SCRAM-SHA-256 (password "secret"), md5_user with md5 ("md5 secret") and
plain_user with the cleartext password method ("plain secret"); the socket trusts every user.
With tls_dir the server also offers TLS with server.pem and server_key.pem from that directory."""
import contextlib
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile


def log_tail(path, lines=60):
    try:
        with open(path, errors='replace') as source:
            return ''.join(source.readlines()[-lines:])
    except OSError as error:
        return f'(no server log: {error})\n'


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


class Server:
    def __init__(self, bindir, root, port):
        self.bindir = bindir
        self.root = root
        self.port = port
        self.socket_dir = root

    def environment(self):
        return {'PGHOST': '127.0.0.1', 'PGPORT': str(self.port), 'PGUSER': 'postgres',
                'PGPASSWORD': 'secret', 'PGDATABASE': 'postgres'}

    def psql(self, sql, database='postgres'):
        """Runs sql as postgres over the socket and returns the output of psql, unaligned."""
        result = subprocess.run([os.path.join(self.bindir, 'psql'), '-X', '-q', '-A', '-t', '-v',
                                 'ON_ERROR_STOP=1', '-h', self.socket_dir, '-p', str(self.port),
                                 '-U', 'postgres', '-d', database, '-c', sql],
                                capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise RuntimeError(f'psql failed: {result.stderr}')
        return result.stdout


@contextlib.contextmanager
def cluster(bindir, tls_dir=None):
    # A driver that is told to stop still stops its cluster: SIGTERM becomes SystemExit, which runs
    # the finally clause below.
    signal.signal(signal.SIGTERM, lambda number, frame: sys.exit(f'stopped by signal {number}'))
    root = tempfile.mkdtemp(prefix='pg')
    data = os.path.join(root, 'data')
    log = os.path.join(root, 'server.log')
    started = False
    try:
        password_file = os.path.join(root, 'password')
        with open(password_file, 'w') as out:
            out.write('secret\n')
        subprocess.run([os.path.join(bindir, 'initdb'), '-D', data, '-U', 'postgres', '-A', 'trust',
                        '--pwfile', password_file, '-E', 'UTF8', '--locale=C', '-N'],
                       check=True, capture_output=True, timeout=120)
        port = free_port()
        with open(os.path.join(data, 'pg_hba.conf'), 'w') as hba:
            hba.write('local all all trust\n'
                      'host all md5_user 127.0.0.1/32 md5\n'
                      'host all md5_user ::1/128 md5\n'
                      'host all plain_user 127.0.0.1/32 password\n'
                      'host all plain_user ::1/128 password\n'
                      'host all all 127.0.0.1/32 scram-sha-256\n'
                      'host all all ::1/128 scram-sha-256\n')
        settings = [f"listen_addresses = 'localhost'", f'port = {port}',
                    f"unix_socket_directories = '{root}'", 'fsync = off', 'max_connections = 40',
                    'log_connections = on', 'log_disconnections = on']
        if tls_dir is not None:
            for name in ('server.pem', 'server_key.pem'):
                shutil.copy(os.path.join(tls_dir, name), os.path.join(data, name))
            os.chmod(os.path.join(data, 'server_key.pem'), 0o600)
            settings += ['ssl = on', "ssl_cert_file = 'server.pem'", "ssl_key_file = 'server_key.pem'"]
        with open(os.path.join(data, 'postgresql.conf'), 'a') as conf:
            conf.write('\n' + '\n'.join(settings) + '\n')
        subprocess.run([os.path.join(bindir, 'pg_ctl'), '-D', data, '-l', log, '-w', '-t', '60', 'start'],
                       check=True, capture_output=True, timeout=120)
        started = True
        server = Server(bindir, root, port)
        server.psql("SET password_encryption = 'md5'; CREATE ROLE md5_user LOGIN PASSWORD 'md5 secret'; "
                    "RESET password_encryption; CREATE ROLE plain_user LOGIN PASSWORD 'plain secret'")
        try:
            yield server
        except BaseException:
            # A failed check shows what the server saw before the cluster and its log go away.
            sys.stderr.write(f'--- end of the server log ---\n{log_tail(log)}')
            raise
    finally:
        if started:
            subprocess.run([os.path.join(bindir, 'pg_ctl'), '-D', data, '-m', 'immediate', '-w', 'stop'],
                           capture_output=True, timeout=120)
        shutil.rmtree(root, ignore_errors=True)
