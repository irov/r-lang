#!/usr/bin/env python3
"""A WebSocket chat room found through SRV, TXT and PTR records of a name server of the same
process (std.websocket, std.dns, std.http upgrade, std.async::broadcast). The accept values,
frames and queries are checked against Python's hashlib, base64 and struct."""
import argparse
import base64
import hashlib
import ipaddress
import os
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = ('realtime demo\nrealtime accept KEY\nrealtime frame TEXT\n'
         'realtime query NAME a|aaaa|srv|txt|ptr\nrealtime reverse ADDRESS\n'
         'realtime resolv FILE\nrealtime answer NAME PORT\n')


def run(values):
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=120)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


checks = 0


def expect(values, status, stdout, stderr=''):
    global checks
    outcome = run(values)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


expect([], 0, '', usage)
expect(['demo', 'extra'], 64, '', usage)
expect(['query', 'example.org', 'mx'], 64, '', usage)
demo = ('srv _chat._tcp.realtime.test -> localhost priority 10 weight 5\n'
        'txt path=/chat\n'
        'txt protocol=chat\n'
        'ptr 127.0.0.1 -> localhost\n'
        'alice <- * alice joined\n'
        'alice <- * bob joined\n'
        'bob <- * bob joined\n'
        'alice <- alice: hello\n'
        'bob <- alice: hello\n'
        'alice <- bob: hi alice\n'
        'bob <- bob: hi alice\n'
        'bob closed 1000\n'
        'alice <- * bob left\n'
        'alice closed 1000\n'
        'upgrade /line -> 101 QUIET WORDS\n'
        'upgrade /chat -> 400\n'
        'connections: 4\n')
for attempt in range(3):
    expect(['demo'], 0, demo)

GUID = b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
for key in ['dGhlIHNhbXBsZSBub25jZQ==', base64.b64encode(os.urandom(16)).decode()]:
    value = base64.b64encode(hashlib.sha1(key.encode() + GUID).digest()).decode()
    expect(['accept', key], 0, value + '\n')


def frame(text):
    payload = text.encode()
    if len(payload) < 126:
        head = struct.pack('!BB', 0x81, len(payload))
    elif len(payload) < 65536:
        head = struct.pack('!BBH', 0x81, 126, len(payload))
    else:
        head = struct.pack('!BBQ', 0x81, 127, len(payload))
    return (head + payload).hex()


for text in ['Hello', 'x' * 125, 'y' * 126, 'Привет, ' * 40]:
    expect(['frame', text], 0, frame(text) + '\n')

TYPES = {'a': 1, 'aaaa': 28, 'srv': 33, 'txt': 16, 'ptr': 12}


def query(name, kind):
    wire = b''.join(bytes([len(label)]) + label.encode() for label in name.rstrip('.').split('.'))
    return (struct.pack('!HHHHHH', 4660, 0x0100, 1, 0, 0, 1) + wire + b'\x00' +
            struct.pack('!HH', TYPES[kind], 1) +
            b'\x00' + struct.pack('!HHIH', 41, 1232, 0, 0)).hex()


for name, kind in [('_chat._tcp.realtime.test', 'srv'), ('example.org.', 'a'),
                   ('1.0.0.127.in-addr.arpa', 'ptr'), ('a' * 63 + '.test', 'aaaa')]:
    expect(['query', name, kind], 0, query(name, kind) + '\n')
expect(['query', 'a..b', 'a'], 65, '', 'dns: invalid_name\n')
expect(['query', 'a' * 64 + '.test', 'txt'], 65, '', 'dns: invalid_name\n')
for text in ['192.0.2.10', '127.0.0.1', '2001:db8::1', '::ffff:192.0.2.1', 'fe80::1:2']:
    address = ipaddress.ip_address(text)
    expect(['reverse', text], 0, f'{address.reverse_pointer} {address.packed.hex()}\n')
expect(['reverse', 'nope'], 65, '', 'address: invalid\n')

with tempfile.TemporaryDirectory() as directory:
    path = os.path.join(directory, 'resolv.conf')
    with open(path, 'w') as conf:
        conf.write('# local\nsearch example.org\nnameserver 192.0.2.53\n'
                   '  nameserver\t2001:db8::35%en0\nnameserver bogus\nnameservers 10.0.0.1\n')
    expect(['resolv', path], 0,
           'nameserver 192.0.2.53 port 53\nnameserver 2001:db8::35 port 53\n')


def wire_name(name):
    return b''.join(bytes([len(label)]) + label.encode() for label in name.rstrip('.').split('.')) + b'\0'


def answer_size(name):
    question = wire_name(name) + struct.pack('!HH', 33, 1)
    data = struct.pack('!HHH', 10, 5, 7001) + wire_name('localhost.')
    record = wire_name(name) + struct.pack('!HHIH', 33, 1, 60, len(data)) + data
    return 12 + len(question) + len(record)


for name in ['_chat._tcp.realtime.test', 'Service.Example.ORG.']:
    shown = name.rstrip('.')
    expect(['answer', name, '7001'], 0,
           f'{shown} ttl 60 srv 10 5 7001 localhost ({answer_size(name)} bytes)\n')
expect(['answer', 'a..b', '1'], 65, '', 'dns: invalid_name\n')
expect(['answer', 'x.test', 'port'], 65, '', 'port: invalid\n')
print(f'{checks} realtime example checks passed')
