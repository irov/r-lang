#!/usr/bin/env python3
"""TLS 1.3 between a client and a server of the same process over loopback TCP (std.tls):
ALPN, an echo, the reasons a client rejects a certificate, and a client that trusts the
certificate authorities of the system (SSL_CERT_FILE, SSL_CERT_DIR or the bundle of the system,
M37, L40)."""
import argparse
import os
import shutil
import subprocess
import tempfile

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


def run(values, bundle=None, directories=None):
    environment = dict(os.environ)
    environment.pop('SSL_CERT_FILE', None)
    environment.pop('SSL_CERT_DIR', None)
    if bundle is not None:
        environment['SSL_CERT_FILE'] = bundle
    if directories is not None:
        environment['SSL_CERT_DIR'] = directories
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=60, env=environment)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


checks = 0


def expect(values, status, stdout, stderr='', bundle=None, directories=None):
    global checks
    outcome = run(values, bundle, directories)
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
# SSL_CERT_DIR lists directories, separated by ':', whose files are read like the bundle, as the
# hashed links of OpenSSL are: the authority behind a link is trusted, a subdirectory and a file
# without certificates are passed over, an empty element is skipped, a missing directory is an
# error of the system, and SSL_CERT_FILE and SSL_CERT_DIR are used together.
scratch = tempfile.mkdtemp(prefix='r-tls-')
try:
    hashed = os.path.join(scratch, 'hashed')
    others = os.path.join(scratch, 'others')
    os.mkdir(hashed)
    os.mkdir(others)
    os.mkdir(os.path.join(hashed, 'nested'))
    shutil.copy(authority, os.path.join(scratch, 'authority.pem'))
    os.symlink(os.path.join(scratch, 'authority.pem'), os.path.join(hashed, '1a2b3c4d.0'))
    shutil.copy(key, os.path.join(hashed, 'key.pem'))
    shutil.copy(other, os.path.join(others, 'other.pem'))
    expect(['echo', 'system', server, key, 'localhost', 'hi'], 0, trusted + 'reply: 2 bytes: hi\n',
           directories=hashed)
    expect(['check', 'system', server, key, 'localhost'], 65, 'rejected: untrusted_certificate\n',
           directories=others)
    expect(['check', 'system', server, key, 'localhost'], 0, trusted, directories=':' + others + ':' + hashed)
    expect(['check', 'system', server, key, 'localhost'], 0, trusted, bundle=other, directories=hashed)
    code, out, err = run(['check', 'system', server, key, 'localhost'], None,
                         os.path.join(scratch, 'missing') + ':' + hashed)
    assert code == 113 and 'not_found' in err, (code, out, err)
    checks += 1
finally:
    shutil.rmtree(scratch)
print(f'tls example checks passed: {checks}')
