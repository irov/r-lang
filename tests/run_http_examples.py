#!/usr/bin/env python3
"""A JSON catalogue served over HTTP and HTTPS on loopback and requested by the std.http client
of the same process (std.http, std.url, std.tls, std.json)."""
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
key = os.path.join(fixtures, 'server_key.pem')
usage = 'http demo CA CERT KEY\nhttp url URL [REFERENCE]\nhttp mime TYPE|PATH\nhttp wire\n'


def run(values):
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=120)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


checks = 0


def expect(values, status, stdout, stderr=''):
    global checks
    outcome = run(values)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


items = ('[{"id":1,"name":"chair","price":45},{"id":2,"name":"lamp","price":30},'
         '{"id":3,"name":"shelf","price":80}]')
expect([], 0, '', usage)
expect(['demo'], 64, '', usage)
expect(['serve', authority, server, key], 64, '', usage)
expect(['demo', authority, server, key], 0,
       'http GET /health -> 200 {"status":"ok"}\n'
       'http GET /items/2 -> 200 {"id":2,"name":"lamp","price":30}\n'
       'http GET /items/9 -> 404 {"error":"no item 9"}\n'
       'http POST /items -> 401 {"error":"missing API key"}\n'
       'http POST /items -> 201 {"id":4,"name":"desk","price":120}\n'
       'http POST /items -> 400 {"error":"invalid item"}\n'
       'http GET /old-items -> 200 ' + items + '\n'
       'https GET /health -> 200 {"status":"ok"}\n'
       'https GET /items/3 -> 200 {"id":3,"name":"shelf","price":80}\n'
       'https event price id=1 chair=45\n'
       'https event price id=2 lamp=30\n'
       'https event price id=3 shelf=80\n'
       'connections: http 1, https 2\n')
# The client trusts another authority: the HTTP requests pass, the first HTTPS one fails.
code, out, err = run(['demo', other, server, key])
assert code == 65 and err == 'tls: untrusted_certificate\n', (code, out, err)
assert out.startswith('http GET /health -> 200'), out
checks += 1
expect(['url', 'HTTP://Example.COM:80/a/./b/../c%7e?x=1&y=a+b#f', '../d?q'], 0,
       'scheme=http host=example.com port=80 path=/a/./b/../c%7e\n'
       'normalized=http://example.com/a/c~?x=1&y=a+b#f\n'
       'query x=1\n'
       'query y=a b\n'
       'resolved=http://example.com:80/d?q\n'
       'form=path=%2Fa%2F.%2Fb%2F..%2Fc%257e encoded=%2Fa%2F.%2Fb%2F..%2Fc%257e '
       'decoded=/a/./b/../c%7e default=true\n')
expect(['url', 'bad url'], 65, 'invalid url: missing_scheme at 0\n')
expect(['url', 'http://host:99999/'], 65, 'invalid url: invalid_port at 12\n')
expect(['mime', 'Text/HTML; Charset="utf-8"'], 0,
       'essence=text/html parameters=1 text=text/html; charset=utf-8\n'
       'charset=utf-8 token=true true\n')
expect(['mime', 'report.PDF'], 0, 'application/pdf (extension true)\n')
expect(['mime', 'README'], 0, 'application/octet-stream (extension false)\n')
expect(['mime', 'text/'], 65, 'invalid media type at 5\n')
expect(['wire'], 0,
       'server: POST /upload body 11 bytes in 2 reads\n'
       'client: 200 Content-Type=text/plain body chunked reply\n'
       'server: second request body 2, answering 204 No Content\n'
       'client: 204 close=true\n')
expect(['wire', 'extra'], 64, '', usage)
print(f'http example checks passed: {checks}')
