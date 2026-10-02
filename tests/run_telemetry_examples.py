#!/usr/bin/env python3
"""Commands of examples/telemetry: one HTTP service on TCP, TLS and a Unix-domain socket at once
(std.service::serve_all through std.http::serve_all), with its metrics at /metrics in the text
exposition format of Prometheus (std.metrics), its health at /health, its spans at /traces
(std.trace), an idle timeout and a drain on SIGTERM. Python clients use each transport, parse the
metrics and the spans, wait for the idle timeout and stop the service with a signal."""
import argparse
import json
import re
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
executable = str(Path(args.executable).resolve())
fixtures = Path(__file__).resolve().parent / 'fixtures' / 'tls'
authority = str(fixtures / 'authority.pem')
certificate = str(fixtures / 'server.pem')
key = str(fixtures / 'server_key.pem')
usage = ('telemetry serve HTTP_PORT TLS_PORT SOCKET CERT KEY\n'
         'telemetry demo CA CERT KEY SOCKET\n'
         'telemetry probe SOCKET\n')
checks = 0


def run(values, cwd='.'):
    result = subprocess.run([executable, *values], capture_output=True, timeout=120, cwd=cwd,
                            stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr='', cwd='.'):
    global checks
    outcome = run(values, cwd)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def unix_get(path, target):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(path)
        client.sendall(f'GET {target} HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n'.encode())
        data = b''
        while chunk := client.recv(4096):
            data += chunk
    head, _, body = data.partition(b'\r\n\r\n')
    return head.split(b'\r\n')[0].decode(), body


def metric_lines(text):
    """The samples of the exposition: name, labels and value; HELP and TYPE lines are checked."""
    samples = {}
    families = {}
    for line in text.splitlines():
        if line.startswith('# HELP '):
            continue
        if line.startswith('# TYPE '):
            _, _, name, kind = line.split(' ')
            families[name] = kind
            continue
        match = re.fullmatch(r'([a-zA-Z_:][a-zA-Z0-9_:]*)(\{[^}]*\})? (\S+)', line)
        assert match, line
        samples[match.group(1) + (match.group(2) or '')] = float(match.group(3))
    return families, samples


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['serve', 'x', '1', 's', 'c', 'k'], 64, '', usage)

with tempfile.TemporaryDirectory() as directory:
    demo = ['http GET /hello -> 200 hello',
            'https GET /items/7 -> 200 {"id":"7"}',
            'unix GET /hello -> HTTP/1.1 200 OK',
            'http GET /health -> 200 ok',
            'metrics in the exposition format: true',
            'telemetry_requests_total{route="/hello"} 2',
            'telemetry_requests_total{route="/items"} 1',
            'telemetry_request_seconds_count 3',
            'spans: 4',
            'serving: true, accepted so far: 3',
            'stopped: accepted 3, completed 3, failed 0, rejected 0, cancelled 0',
            'serving after the signal: false']
    expect(['demo', authority, certificate, key, 'demo.sock'], 0, '\n'.join(demo) + '\n', cwd=directory)
    expect(['probe', 'probe.sock'], 0, 'listener 0\nprobe: accepted 1 of up to 16 listeners\n', cwd=directory)

    http_port, tls_port = free_port(), free_port()
    server = subprocess.Popen([executable, 'serve', str(http_port), str(tls_port), 'service.sock', certificate,
                               key], cwd=directory, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
    try:
        ready = server.stdout.readline().decode()
        assert ready == f'ready http {http_port} tls {tls_port} unix service.sock\n', ready
        base = f'http://127.0.0.1:{http_port}'
        with urllib.request.urlopen(f'{base}/hello', timeout=10) as answer:
            assert (answer.status, answer.read()) == (200, b'hello\n')
        context = ssl.create_default_context(cafile=authority)
        with urllib.request.urlopen(f'https://localhost:{tls_port}/items/42', timeout=10,
                                    context=context) as answer:
            assert answer.status == 200 and json.loads(answer.read()) == {'id': '42'}
        status_line, body = unix_get(str(Path(directory) / 'service.sock'), '/hello')
        assert (status_line, body) == ('HTTP/1.1 200 OK', b'hello\n'), (status_line, body)
        with urllib.request.urlopen(f'{base}/health', timeout=10) as answer:
            assert (answer.status, answer.read()) == (200, b'ok\n')
        with urllib.request.urlopen(f'{base}/metrics', timeout=10) as answer:
            assert answer.headers['Content-Type'] == 'text/plain; version=0.0.4; charset=utf-8'
            families, samples = metric_lines(answer.read().decode())
        assert families == {'telemetry_requests_total': 'counter', 'telemetry_requests_in_flight': 'gauge',
                            'telemetry_request_seconds': 'histogram'}, families
        assert samples['telemetry_requests_total{route="/hello"}'] == 2
        assert samples['telemetry_requests_total{route="/items"}'] == 1
        assert samples['telemetry_requests_in_flight'] == 0
        assert samples['telemetry_request_seconds_count'] == 3
        assert samples['telemetry_request_seconds_bucket{le="+Inf"}'] == 3
        assert samples['telemetry_request_seconds_bucket{le="0.005"}'] <= 3
        checks += 1
        with urllib.request.urlopen(f'{base}/traces', timeout=10) as answer:
            spans = [json.loads(line) for line in answer.read().decode().splitlines()]
            assert answer.headers['X-Dropped-Spans'] == '0'
            assert answer.headers['X-Span-Attributes'] == '3'
        names = sorted(span['name'] for span in spans)
        assert names == ['GET /hello', 'GET /hello', 'GET /items', 'lookup'], names
        lookup = [span for span in spans if span['name'] == 'lookup'][0]
        parent = [span for span in spans if span['name'] == 'GET /items'][0]
        assert lookup['parent'] == parent['id'] and lookup['attributes'] == {'id': '42'}
        assert all(span['duration_us'] >= 0 and span['task'] > 0 for span in spans)
        checks += 1
        # A connection that sends nothing is closed after the idle timeout of two seconds.
        started = time.monotonic()
        with socket.create_connection(('127.0.0.1', http_port), timeout=10) as idle:
            assert idle.recv(16) == b''
        waited = time.monotonic() - started
        assert 1.5 <= waited < 8, waited
        checks += 1
        server.send_signal(signal.SIGTERM)
        out, err = server.communicate(timeout=30)
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()
    assert server.returncode == 0, (server.returncode, err)
    # The HTTP server ends a connection whose read waited past the idle timeout like one that the
    # client closed, so its handler completes.
    assert out.decode() == 'stopped: accepted 7, completed 7, failed 0, rejected 0, cancelled 0\n', out
    checks += 1

print(f'telemetry examples: {checks} checks passed')
