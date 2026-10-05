#!/usr/bin/env python3
"""TLS 1.3 between a client and a server of the same process over loopback TCP (std.tls):
ALPN, an echo, the reasons a client rejects a certificate, and a client that trusts the
certificate authorities of the system (SSL_CERT_FILE or the bundle of the system, M37)."""
import argparse
import os
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
fixtures = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures', 'tls')
authority = os.path.join(fixtures, 'authority.pem')
other = os.path.join(fixtures, 'other_authority.pem')
server = os.path.join(fixtures, 'server.pem')
expired = os.path.join(fixtures, 'expired.pem')
key = os.path.join(fixtures, 'server_key.pem')
usage = 'tls echo CA CERT KEY NAME WORD...\ntls check CA CERT KEY NAME\n'


def run(values, bundle=None):
    environment = dict(os.environ)
    environment.pop('SSL_CERT_FILE', None)
    if bundle is not None:
        environment['SSL_CERT_FILE'] = bundle
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=60, env=environment)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


checks = 0


def expect(values, status, stdout, stderr='', bundle=None):
    global checks
    outcome = run(values, bundle)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


trusted = 'trusted: protocol=echo/1 version=tls13\n'
expect([], 0, '', usage)
expect(['echo', authority], 64, '', usage)
expect(['bogus', authority, server, key, 'localhost'], 64, '', usage)
expect(['echo', authority, server, key, 'localhost', 'hello', 'world'], 0,
       trusted + 'reply: 11 bytes: hello world\n')
expect(['check', authority, server, key, 'localhost'], 0, trusted)
expect(['check', authority, server, key, '127.0.0.1'], 0, trusted)
expect(['check', authority, server, key, 'example.org'], 65, 'rejected: name_mismatch\n')
expect(['check', other, server, key, 'localhost'], 65, 'rejected: untrusted_certificate\n')
expect(['check', authority, expired, key, 'localhost'], 65, 'rejected: expired_certificate\n')
expect(['check', key, server, key, 'localhost'], 65, '', 'tls: invalid_certificate\n')
expect(['check', authority, server, authority, 'localhost'], 65, '', 'tls: invalid_key\n')
code, out, err = run(['check', os.path.join(fixtures, 'missing.pem'), server, key, 'localhost'])
assert code == 113 and 'not_found' in err, (code, out, err)
checks += 1
# The authorities of the system do not include the test authority; SSL_CERT_FILE replaces them,
# a bundle without certificates trusts nothing and a missing one is an error of the system.
expect(['check', 'system', server, key, 'localhost'], 65, 'rejected: untrusted_certificate\n')
expect(['echo', 'system', server, key, 'localhost', 'hi'], 0, trusted + 'reply: 2 bytes: hi\n',
       bundle=authority)
expect(['check', 'system', server, key, 'localhost'], 65, 'rejected: untrusted_certificate\n', bundle=other)
expect(['check', 'system', server, key, 'localhost'], 65, '', 'tls: missing_authorities\n', bundle=key)
code, out, err = run(['check', 'system', server, key, 'localhost'], os.path.join(fixtures, 'missing.pem'))
assert code == 113 and 'not_found' in err, (code, out, err)
checks += 1
print(f'tls example checks passed: {checks}')
