#!/usr/bin/env python3
"""Commands of examples/arena, the back end of a mobile game: ES256 session tokens of std.jwt in
the shape the game clients read, HS256 administrator tokens, a JWK Set, and access tokens
of std.oauth2 from a token endpoint that this driver serves on loopback. Tokens are compared with
an independent JWS made with the Python `cryptography` package; the endpoint verifies the RS256
assertion of the service account. With --bin, the driver runs the database commands instead,
against a temporary PostgreSQL cluster whose tables psql checks."""
import argparse
import base64
import datetime
import hashlib
import hmac
import json
import http.client
import os
import re
import signal
import subprocess
import tempfile
import threading
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives import padding as block_padding
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
parser.add_argument('--bin', help='directory of initdb, pg_ctl and psql: run the database commands')
args = parser.parse_args()
usage = ('arena token KEY ACCOUNT NICK EMAIL PLATFORM PROVIDER AVATAR | check KEY TOKEN | jwks KEY\n'
         'arena admin SECRET SUBJECT SECONDS | admin_check SECRET TOKEN\n'
         'arena google KEY_FILE SCOPE | client ENDPOINT CLIENT SECRET | login ENDPOINT CODE VERIFIER\n'
         'arena config\n'
         'arena migrate | register ACCOUNT NICK EMAIL CREATED | ban ID TYPE AT | sync ID AT SESSION BADGES STATS\n'
         'arena users | user ID | server KEY CIPHER\n')
checks = 0


def run(values, environment=None):
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=120, env=environment,
                            stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr='', environment=None):
    global checks
    outcome = run(values, environment)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


def unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + '=' * (-len(text) % 4))


def compact(value) -> bytes:
    return json.dumps(value, separators=(',', ':')).encode()


def session_token(key, claims):
    """An ES256 session token of the server, made with the Python `cryptography` package."""
    signing_input = b64(compact({'alg': 'ES256', 'typ': 'JWT', 'kid': 'arena-1'})) + '.' + b64(compact(claims))
    r, s = decode_dss_signature(key.sign(signing_input.encode(), ec.ECDSA(hashes.SHA256(), deterministic_signing=True)))
    return signing_input + '.' + b64(r.to_bytes(32, 'big') + s.to_bytes(32, 'big'))


def write_key(key, name):
    path = os.path.join(directory, name)
    with open(path, 'wb') as handle:
        handle.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                       serialization.NoEncryption()))
    return path


directory = tempfile.mkdtemp(prefix='arena-')
expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['check', 'only-key'], 64, '', usage)


def database_checks():
    """The users table: migrations, registration by struct, typed parameters and reads, bans by an
    UPDATE made from a struct, and a changed migration that the next start refuses."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from postgres_cluster import cluster  # noqa: E402
    tls = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures', 'tls')
    with cluster(args.bin, tls) as database:
        environment = dict(os.environ)
        environment.update(database.environment())
        expect(['users'], 65, '', 'database: server 42P01\n', environment)
        expect(['migrate'], 0, 'applied 4 migrations\n', environment=environment)
        expect(['migrate'], 0, 'applied 0 migrations\n', environment=environment)
        assert database.psql('SELECT version, name FROM r_schema_migrations ORDER BY version') == \
            '1|users\n2|sessions\n3|gems\n4|commands\n'
        expect(['register', '42', 'Ann', 'ann@example.test', '2024-03-10T07:00:00Z'], 0, 'user 1\n',
               environment=environment)
        expect(['register', '43', 'Борис', 'boris@example.test', '2024-03-11T23:30:00+05:00'], 0, 'user 2\n',
               environment=environment)
        expect(['register', '44', 'Ann', 'x@example.test', '2024-03-12T00:00:00Z'], 65, '',
               'database: server 23505\n', environment)
        assert database.psql("SELECT account_id, avatar, extract(epoch FROM created_at)::bigint FROM users "
                             "ORDER BY id") == '42|1_avatar|1710054000\n43|1_avatar|1710181800\n'
        expect(['user', '1'], 0, 'Ann joined 2024-03-10\nnever synced\n', environment=environment)
        expect(['sync', '1', '2024-04-01T12:00:00.5Z', '0190f7e1-1234-7abc-8def-0123456789ab', 'first win,ёж "q"',
                '{"wins": 3, "losses": 1}'], 0, 'synced 1\n', environment=environment)
        expect(['sync', '2', '2024-04-01T12:00:00Z', '0190f7e1-1234-7abc-8def-0123456789ac', '', '{"wins": 0}'], 0,
               'synced 1\n', environment=environment)
        assert database.psql("SELECT extract(epoch FROM last_sync), session, badges, stats FROM users "
                             "WHERE id = 1") == \
            '1711972800.500000|0190f7e1-1234-7abc-8def-0123456789ab|{"first win","ёж \\"q\\""}|' \
            '{"wins": 3, "losses": 1}\n'
        expect(['user', '1'], 0, 'Ann joined 2024-03-10\nlast sync 2024-04-01T12:00:00Z session '
               '0190f7e1-1234-7abc-8def-0123456789ab\nbadges [first win] [ёж "q"]\nstats wins losses\n',
               environment=environment)
        expect(['user', '2'], 0, 'Борис joined 2024-03-11\nlast sync 2024-04-01T12:00:00Z session '
               '0190f7e1-1234-7abc-8def-0123456789ac\nbadges\nstats wins\n', environment=environment)
        expect(['user', '9'], 65, '', 'no user 9\n', environment)
        expect(['ban', '2', 'suspicious', '2024-04-02T00:00:00Z'], 0,
               'UPDATE users SET "ban_type" = $2, "last_ban_check" = $3 WHERE "id" = $1\nchanged 1\n',
               environment=environment)
        expect(['ban', '2', 'forever', '2024-04-02T00:00:00Z'], 64, '', usage, environment)
        database.psql("UPDATE users SET clan_id = 7, gems = 12.50 WHERE id = 1")
        expect(['users'], 0, '1 42 Ann none gems 12.5 joined 1710054000 clan 7 badges first win,ёж "q" '
               'stats {"wins":3,"losses":1}\n2 43 Борис suspicious gems 0 joined 1710181800 clan - badges - '
               'stats {"wins":0}\n', environment=environment)
        # A NULL element is none to the typed read, and no string to a field of User.
        database.psql("UPDATE users SET badges = ARRAY['a', NULL] WHERE id = 2")
        expect(['user', '2'], 0, 'Борис joined 2024-03-11\nlast sync 2024-04-01T12:00:00Z session '
               '0190f7e1-1234-7abc-8def-0123456789ac\nbadges [a] NULL\nstats wins\n', environment=environment)
        expect(['users'], 65, '', 'database: invalid_value: a row does not read as the struct\n', environment)
        database.psql("ALTER TABLE users RENAME COLUMN nickname TO nick")
        expect(['users'], 65, '', 'database: missing_column nickname\n', environment)
        database.psql("ALTER TABLE users RENAME COLUMN nick TO nickname")
        server_checks(database, environment)
        # An applied migration that changed is refused before anything runs.
        database.psql("UPDATE r_schema_migrations SET checksum = 'x' WHERE version = 2")
        expect(['migrate'], 65, '', 'database: migration_mismatch version 2\n', environment)


def seal(key, plain):
    """A body of the secure routes: a random IV, then AES-256-CBC with PKCS#7 padding."""
    iv = os.urandom(16)
    padder = block_padding.PKCS7(128).padder()
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return iv + encryptor.update(padder.update(plain) + padder.finalize()) + encryptor.finalize()


def unseal(key, sealed):
    decryptor = Cipher(algorithms.AES(key), modes.CBC(sealed[:16])).decryptor()
    unpadder = block_padding.PKCS7(128).unpadder()
    return unpadder.update(decryptor.update(sealed[16:]) + decryptor.finalize()) + unpadder.finalize()


def server_checks(database, environment):
    """The HTTP application of the server, as a client of this driver sees it on one kept-alive
    connection: the middleware of sessions, request identifiers, CORS, the sealed protocol and the
    commands in transactions, a handler that panics, and the routing rules; then the log of the
    server, one JSON record per request."""
    global checks
    key = ec.generate_private_key(ec.SECP256R1())
    key_path = write_key(key, 'server.pem')
    token = session_token(key, {'AccountID': '42', 'NickName': 'Ann', 'Email': 'ann@example.test',
                                'Platform': 'ios', 'Provider': 'apple', 'Avatar': '7'})
    forged = session_token(ec.generate_private_key(ec.SECP256R1()), {'AccountID': '42'})
    cipher = bytes(range(32))
    server = subprocess.Popen([args.executable, 'server', key_path, cipher.hex()], env=environment,
                              stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        ready = server.stdout.readline().decode()
        assert ready.startswith('ready '), ready
        connection = http.client.HTTPConnection('127.0.0.1', int(ready.split()[1]), timeout=30)
        bearer = {'Authorization': 'Bearer ' + token}
        sent = 0

        def call(method, target, body=None, headers=None):
            """A request with the identifier req-N, N counting the requests that are not preflight
            requests, except where the headers say otherwise."""
            nonlocal sent
            fields = dict(headers or {})
            if method != 'OPTIONS':
                sent += 1
                fields.setdefault('X-Request-ID', f'req-{sent}')
            fields = {name: value for name, value in fields.items() if value is not None}
            connection.request(method, target, body=body, headers=fields)
            answer = connection.getresponse()
            return answer.status, answer.headers, answer.read()

        def expect_json(outcome, status, value, request_id=None):
            global checks
            assert outcome[0] == status and json.loads(outcome[2]) == value, outcome
            if request_id is not None:
                assert outcome[1]['X-Request-ID'] == request_id, outcome[1]
            checks += 1

        # Sessions from a bearer token or the cookie; a request without an identifier gets a UUID.
        status, fields, body = call('GET', '/api/me', headers={'X-Request-ID': None})
        assert (status, json.loads(body)) == (401, {'error': 'unauthorized'}), (status, body)
        generated = fields['X-Request-ID']
        assert re.fullmatch(r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}', generated), generated
        checks += 1
        expect_json(call('GET', '/api/me', headers={'Cookie': 'theme=dark; arena_session=' + token}), 200,
                    {'account': '42', 'request': 2}, 'req-2')
        expect_json(call('GET', '/api/me', headers={'Authorization': 'Bearer ' + forged}), 401,
                    {'error': 'unauthorized'})
        status, fields, body = call('POST', '/api/session', headers=bearer)
        assert (status, body) == (204, b'') and fields['Set-Cookie'] == \
            f'arena_session={token}; Path=/api; Max-Age=2592000; Secure; HttpOnly; SameSite=Strict', fields
        checks += 1
        # The most specific route answers whatever the order of registration; a final '/' is
        # redirected, a wrong method and an unknown path are 404.
        expect_json(call('GET', '/api/users/me', headers=bearer), 200, {'account': '42', 'request': 5})
        expect_json(call('GET', '/api/users/1', headers=bearer), 200, {'id': 1, 'nickname': 'Ann'})
        status, fields, body = call('GET', '/api/users/1/', headers=bearer)
        assert (status, fields['Location']) == (301, '/api/users/1'), (status, fields)
        assert call('DELETE', '/api/me', headers=bearer)[0] == 404
        status, fields, body = call('GET', '/nowhere')
        assert (status, fields['X-Request-ID']) == (404, f'req-{sent}'), (status, fields)
        checks += 1
        # Background work that panics after the answer: nothing observes it, so its report goes to
        # the listener of the server and from there into the log.
        status, fields, body = call('POST', '/api/background', b'', bearer)
        assert (status, json.loads(body)) == (202, {'queued': True}), (status, body)
        checks += 1
        # CORS: the server answers a preflight request of the game's origin itself and marks the
        # responses to its requests; another origin is refused.
        status, fields, body = call('OPTIONS', '/api/commands/nick', headers={
            'Origin': 'https://game.example', 'Access-Control-Request-Method': 'POST',
            'Access-Control-Request-Headers': 'X-Command-ID'})
        assert status == 204 and fields['Access-Control-Allow-Origin'] == 'https://game.example' and \
            fields['Access-Control-Allow-Credentials'] == 'true' and \
            fields['Access-Control-Allow-Methods'] == 'POST' and \
            fields['Access-Control-Allow-Headers'] == 'X-Command-ID' and \
            fields['Access-Control-Max-Age'] == '600', (status, fields)
        assert call('OPTIONS', '/api/me', headers={'Origin': 'https://evil.example',
                                                   'Access-Control-Request-Method': 'GET'})[0] == 403
        status, fields, body = call('GET', '/api/me', headers=dict(bearer, Origin='https://game.example'))
        assert status == 200 and fields['Access-Control-Allow-Origin'] == 'https://game.example' and \
            fields['Vary'] == 'Origin', (status, fields)
        checks += 1
        # A command runs once: a repeated command id gets the kept answer, whatever its body.
        command = dict(bearer, **{'Content-Type': 'application/json'})
        expect_json(call('POST', '/api/commands/nick', b'{"nick":"Hero"}', dict(command, **{'X-Command-ID': 'c-1'})),
                    200, {'nick': 'Hero'})
        status, fields, body = call('POST', '/api/commands/nick', b'{"nick":"Villain"}',
                                    dict(command, **{'X-Command-ID': 'c-1'}))
        assert (status, json.loads(body), fields['X-Replayed']) == (200, {'nick': 'Hero'}, 'true'), (status, body)
        checks += 1
        expect_json(call('POST', '/api/commands/nick', b'{"nick":"Hero"}', command), 400, {'error': 'command id'})
        expect_json(call('POST', '/api/commands/nick', b'{"nick":"\xd0\x91\xd0\xbe\xd1\x80\xd0\xb8\xd1\x81"}',
                         dict(command, **{'X-Command-ID': 'c-2'})), 409, {'error': 'nick taken'})
        expect_json(call('POST', '/api/commands/nick', b'nick', dict(command, **{'X-Command-ID': 'c-3'})), 400,
                    {'error': 'nick'})
        assert database.psql("SELECT command_id, status, body FROM client_commands ORDER BY command_id") == \
            'c-1|200|{"nick":"Hero"}\nc-3|400|{"error":"nick"}\n'
        # A handler that panics inside its command: 500, the change is not kept, and the next
        # command on the same connection of the pool rolls the open transaction back first.
        status, fields, body = call('POST', '/api/commands/crash', b'', dict(command, **{'X-Command-ID': 'c-4'}))
        assert (status, body) == (500, b'Internal Server Error'), (status, body)
        assert database.psql("SELECT nickname FROM users WHERE id = 1") == 'Hero\n'
        expect_json(call('POST', '/api/commands/nick', b'{"nick":"Champion"}', dict(command, **{'X-Command-ID': 'c-5'})),
                    200, {'nick': 'Champion'})
        assert database.psql("SELECT nickname FROM users WHERE id = 1") == 'Champion\n'
        assert database.psql("SELECT count(*) FROM client_commands WHERE command_id = 'c-4'") == '0\n'
        checks += 1
        # Answers escape `<`, `>` and `&`, which read back unchanged.
        status, fields, body = call('POST', '/api/commands/nick', b'{"nick":"<Hero&Co>"}',
                                    dict(command, **{'X-Command-ID': 'c-6'}))
        assert (status, body) == (200, b'{"nick":"\\u003cHero\\u0026Co\\u003e"}'), (status, body)
        assert json.loads(body) == {'nick': '<Hero&Co>'}
        checks += 1
        # The secure routes: both bodies sealed with AES-256-CBC under the shared key.
        status, fields, body = call('POST', '/api/secure/echo', seal(cipher, 'ping ✓'.encode()), bearer)
        assert status == 200 and fields['Content-Type'] == 'application/octet-stream', (status, fields)
        assert json.loads(unseal(cipher, body)) == {'echo': 'ping ✓', 'account': '42'}, body
        status, fields, body = call('POST', '/api/secure/echo', b'short', bearer)
        assert status == 400 and json.loads(unseal(cipher, body)) == {'error': 'sealed body'}, (status, body)
        expect_json(call('POST', '/api/secure/echo', seal(cipher, b'x')), 401, {'error': 'unauthorized'})
        connection.close()
        server.send_signal(signal.SIGTERM)
        out, err = server.communicate(timeout=30)
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()
    assert (server.returncode, out.decode()) == (
        0, 'stopped: accepted 1, failed 0, requests 22, panics 1, free connections 2, log lines 23\n'), (sent, out, err)
    checks += 1
    # The log: one record per request in the order the requests were answered, level first, then
    # the bound fields, those of the record, the time and the message.
    records = [json.loads(line, object_pairs_hook=list) for line in err.decode().splitlines()]
    assert len(records) == 23, err
    names = [[name for name, _ in record] for record in records]
    values = [dict(record) for record in records]
    assert names[0] == ['level', 'service', 'component', 'request_id', 'http.method', 'http.route',
                        'http.status_code', 'timestamp', 'message'], names[0]
    assert values[0]['request_id'] == generated and values[0]['level'] == 'warn', values[0]
    assert names[1] == ['level', 'service', 'component', 'request_id', 'usr.id', 'http.method', 'http.route',
                        'http.status_code', 'timestamp', 'message'], names[1]
    assert values[1] == dict(values[1], level='info', service='arena', component='http', request_id='req-2',
                             **{'usr.id': '42', 'http.method': 'GET', 'http.route': '/api/me',
                                'http.status_code': 200, 'message': 'http request completed'}), values[1]
    for record in values:
        stamp = record['timestamp']
        assert re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d{0,8}[1-9])?Z', stamp), stamp
        assert abs(datetime.datetime.fromisoformat(stamp.replace('Z', '+00:00')).timestamp() - time.time()) < 300
    routes = [(record['http.method'], record['http.route'], record['http.status_code'], record['level'])
              for record in values if record['message'] == 'http request completed']
    assert ('GET', '/api/users/{id}', 200, 'info') in routes and ('GET', '/api/users/1/', 301, 'info') in routes and \
        ('DELETE', '/api/me', 404, 'warn') in routes and ('POST', '/api/commands/nick', 409, 'warn') in routes, routes
    unobserved = [dict(record) for record in records if dict(record)['message'] == 'panic recovered' and
                  'request_id' not in dict(record)]
    assert len(unobserved) == 1 and [name for name, _ in records[[dict(r) for r in records].index(unobserved[0])]] == [
        'level', 'service', 'component', 'panic', 'place', 'severity', 'timestamp', 'message'], unobserved
    assert unobserved[0]['panic'] == 'explicit: a background reward failed' and \
        re.fullmatch(r'module \d+ bytes \[\d+,\d+\)', unobserved[0]['place']), unobserved
    panicked = [record for record in records if dict(record)['message'] == 'panic recovered' and
                'request_id' in dict(record)]
    assert len(panicked) == 1 and [name for name, _ in panicked[0]] == [
        'level', 'service', 'component', 'request_id', 'usr.id', 'http.method', 'http.route', 'http.status_code',
        'panic', 'severity', 'timestamp', 'message'], panicked
    assert dict(panicked[0]) == dict(dict(panicked[0]), level='error', request_id='req-17', **{
        'usr.id': '42', 'http.method': 'POST', 'http.route': '/api/commands/crash', 'http.status_code': 500,
        'panic': 'explicit: the crash command', 'severity': 'critical'}), panicked
    checks += 1


if args.bin:
    database_checks()
    print(f'arena database examples: {checks} checks passed')
    sys.exit(0)

# The server key in the SEC1 form (EC PRIVATE KEY).
server_key = ec.generate_private_key(ec.SECP256R1())
key_path = write_key(server_key, 'private_auth_key.pem')

# A session token is deterministic (RFC 6979): it equals the one made here byte for byte.
claims = {'AccountID': '42', 'NickName': 'Ann', 'Email': 'ann@example.test', 'Platform': 'ios',
          'Provider': 'apple', 'Avatar': '7'}
token = session_token(server_key, claims)
expect(['token', key_path, '42', 'Ann', 'ann@example.test', 'ios', 'apple', '7'], 0, token + '\n')
session = 'alg ES256 kid arena-1\naccount 42 nick Ann platform ios provider apple\n'
expect(['check', key_path, token], 0, session)
header, payload, signature = token.split('.')
forged = header + '.' + b64(compact(dict(claims, AccountID='1'))) + '.' + signature
expect(['check', key_path, forged], 65, 'rejected: invalid_signature\n')
other_key = os.path.join(directory, 'other.pem')
with open(other_key, 'wb') as handle:
    handle.write(ec.generate_private_key(ec.SECP256R1()).private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
expect(['check', other_key, token], 65, 'rejected: invalid_signature\n')
expect(['check', key_path, 'not.a.token'], 65, 'rejected: malformed\n')

# The JWK Set of the public key.
numbers = server_key.public_key().public_numbers()
code, out, err = run(['jwks', key_path])
assert code == 0 and err == '', (code, out, err)
assert json.loads(out) == {'keys': [{'kty': 'EC', 'crv': 'P-256', 'x': b64(numbers.x.to_bytes(32, 'big')),
                                     'y': b64(numbers.y.to_bytes(32, 'big')), 'alg': 'ES256', 'use': 'sig',
                                     'kid': 'arena-1'}]}, out
checks += 1

# Administrator tokens with a shared HS256 secret, checked here and there.
secret = 'an administrator secret of 32 by'
code, out, err = run(['admin', secret, 'operator', '300'])
assert code == 0 and err == '', (code, out, err)
admin_token = out.strip()
parts = admin_token.split('.')
assert hmac.compare_digest(hmac.new(secret.encode(), (parts[0] + '.' + parts[1]).encode(), hashlib.sha256).digest(),
                           unb64(parts[2]))
admin_claims = json.loads(unb64(parts[1]))
assert admin_claims['iss'] == 'arena-admin' and admin_claims['sub'] == 'operator', admin_claims
assert abs(admin_claims['exp'] - (time.time() + 300)) < 30, admin_claims
checks += 1
expect(['admin_check', secret, admin_token], 0, compact(admin_claims).decode() + '\n')
expect(['admin_check', secret[:-1] + 'X', admin_token], 65, 'rejected: invalid_signature\n')
code, out, err = run(['admin', secret, 'operator', '-120'])
assert code == 0, (code, out, err)
expect(['admin_check', secret, out.strip()], 65, 'rejected: expired\n')
expect(['admin', 'short', 'operator', '300'], 65, '', 'token: key_mismatch\n')

# A token endpoint on loopback for the service account, the admin panel and a player's login.
account_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
requests = []


class Endpoint(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def answer(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def granted(self, prefix, refresh=None, expires_in=3600):
        body = {'access_token': f'{prefix}-{len(requests)}', 'token_type': 'Bearer', 'expires_in': expires_in}
        if refresh:
            body['refresh_token'] = refresh
        self.answer(200, body)

    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        form = dict(urllib.parse.parse_qsl(self.rfile.read(length).decode()))
        requests.append(form)
        kind = form.get('grant_type')
        if kind == 'urn:ietf:params:oauth:grant-type:jwt-bearer':
            head, body, signature = form['assertion'].split('.')
            try:
                account_key.public_key().verify(unb64(signature), (head + '.' + body).encode(), padding.PKCS1v15(),
                                                hashes.SHA256())
            except InvalidSignature:
                return self.answer(400, {'error': 'invalid_grant'})
            header_value, claims_value = json.loads(unb64(head)), json.loads(unb64(body))
            good = (header_value == {'alg': 'RS256', 'typ': 'JWT', 'kid': 'key-1'} and
                    claims_value['iss'] == 'robot@arena.test' and claims_value['aud'] == endpoint and
                    claims_value['scope'] == 'https://www.googleapis.com/auth/androidpublisher' and
                    claims_value['exp'] == claims_value['iat'] + 3600)
            return self.granted('sa') if good else self.answer(400, {'error': 'invalid_grant'})
        if kind == 'client_credentials':
            expected = 'Basic ' + base64.b64encode(b'panel:pa%3Ass').decode()
            if self.headers.get('Authorization') != expected or form.get('scope') != 'admin':
                return self.answer(401, {'error': 'invalid_client', 'error_description': 'unknown client'})
            return self.granted('cc')
        if kind == 'authorization_code':
            if (form.get('code'), form.get('code_verifier'), form.get('client_id'), form.get('redirect_uri')) != \
                    ('c0de', 'pkce-verifier', 'arena-app', 'arena://login'):
                return self.answer(400, {'error': 'invalid_grant'})
            return self.granted('code', 'r1', 1)
        if kind == 'refresh_token':
            if form.get('refresh_token') == 'r1':
                return self.granted('refreshed', 'r2')
            if form.get('refresh_token') == 'r2':
                return self.granted('rotated')
            return self.answer(400, {'error': 'invalid_grant'})
        return self.answer(400, {'error': 'unsupported_grant_type'})


server = ThreadingHTTPServer(('127.0.0.1', 0), Endpoint)
threading.Thread(target=server.serve_forever, daemon=True).start()
endpoint = f'http://127.0.0.1:{server.server_address[1]}/token'
try:
    key_file = os.path.join(directory, 'service-account.json')
    with open(key_file, 'w') as handle:
        json.dump({'type': 'service_account', 'project_id': 'arena', 'private_key_id': 'key-1',
                   'private_key': account_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                                            serialization.NoEncryption()).decode(),
                   'client_email': 'robot@arena.test', 'token_uri': endpoint}, handle)
    expect(['google', key_file, 'https://www.googleapis.com/auth/androidpublisher'], 0,
           'account robot@arena.test\nfirst sa-1\nagain sa-1\nrenewed sa-2\n')
    expect(['client', endpoint, 'panel', 'pa:ss'], 0, 'token cc-3 type Bearer expires in 3600\n')
    expect(['client', endpoint, 'panel', 'wrong'], 0, 'refused: endpoint_error 401 invalid_client\n')
    expect(['login', endpoint, 'c0de', 'pkce-verifier'], 0,
           'code code-5\nrefreshed refreshed-6\ncached refreshed-6\nrenewed rotated-7\n')
    expect(['login', endpoint, 'other', 'pkce-verifier'], 69, '', 'oauth2: endpoint_error 400\n')
    with open(key_file, 'w') as handle:
        json.dump({'type': 'authorized_user', 'client_id': 'x'}, handle)
    expect(['google', key_file, 'scope'], 69, '', 'oauth2: invalid_key_file 0\n')
finally:
    server.shutdown()
# The settings of the server from its environment variables, and its tuning keys. The
# variables the server reads come from settings alone; the rest of the environment, such as PATH
# and the options of a sanitizer runtime, is kept.
read_by_server = ('PG_', 'API_KEY', 'ENVIRONMENT', 'PROD', 'DD_', 'HOST_PORT', 'ARENA_')
kept = {name: value for name, value in os.environ.items() if not name.startswith(read_by_server)}


def with_settings(values):
    return dict(kept, **values)


settings = {'PG_HOST': 'db.internal', 'PG_DB_NAME': 'game', 'PG_USER': 'svc', 'PG_PASSWORD': 'pw',
            'API_KEY': 'k', 'ENVIRONMENT': 'eu1', 'PROD': 'TRUE', 'DD_SERVICE': 'arena-backend-eu1',
            'DD_ENV': 'eu1', 'PG_PORT': '6432', 'ARENA_POOL_SIZE': '16', 'ARENA_TIME_ZONE': 'Asia/Shanghai'}
expect(['config'], 0, 'database svc@db.internal:6432/game\nenvironment eu1 production=true\nlisten on :8080\n'
       'datadog service=arena-backend-eu1 env=eu1 project=arena\n'
       'pool 16 connections, requests up to 1048576 bytes, rewards in Asia/Shanghai\nseason ends 2026-12-31 23:59\n', environment=with_settings(settings))
expect(['config'], 0, 'database svc@db.internal:6432/game\nenvironment eu1 production=true\nlisten on :9000\n'
       'datadog service=arena-backend-eu1 env=eu1 project=tournament\n'
       'pool 16 connections, requests up to 1048576 bytes, rewards in Asia/Shanghai\nseason ends 2026-12-31 23:59\n',
       environment=with_settings(dict(settings, HOST_PORT='9000', DD_PROJECT='tournament')))
expect(['config'], 78, '', 'config: missing_value DD_ENV\n',
       environment=with_settings({name: value for name, value in settings.items() if name != 'DD_ENV'}))
expect(['config'], 78, '', 'config: invalid_value PG_PORT\n', environment=with_settings(dict(settings, PG_PORT='70000')))
expect(['config'], 78, '', 'config: invalid_value PROD\n', environment=with_settings(dict(settings, PROD='perhaps')))
expect(['config'], 78, '', 'config: invalid_value pool_size\n', environment=with_settings(dict(settings, ARENA_POOL_SIZE='many')))
expect(['config'], 0, 'database svc@db.internal:6432/game\nenvironment eu1 production=true\nlisten on :8080\n'
       'datadog service=arena-backend-eu1 env=eu1 project=arena\n'
       'pool 16 connections, requests up to 1048576 bytes, rewards in Asia/Shanghai\nseason ends 2027-01-15 06:00\n',
       environment=with_settings(dict(settings, ARENA_SEASON_END='15.01.2027 06:00')))
expect(['config'], 78, '', 'config: invalid_value season_end\n',
       environment=with_settings(dict(settings, ARENA_SEASON_END='31.02.2027 06:00')))
print(f'arena examples: {checks} checks passed')
