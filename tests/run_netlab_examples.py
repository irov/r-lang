#!/usr/bin/env python3
"""Exercise numeric addresses, real loopback TCP/UDP, socket options and connection by name
without external services."""
import argparse
import ipaddress
import subprocess
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, status=0, expected=None):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=12)
        assert result.returncode == status and not result.stderr, ([str(arg)[:80] for arg in args], result.returncode, result.stdout, result.stderr)
        if expected is not None:
            assert result.stdout == expected, ([str(arg)[:80] for arg in args], result.stdout, expected)
        count += 1
        return result.stdout

    for text in ['127.0.0.1', '0.0.0.0', '255.255.255.255', '::', '::1',
                 '2001:0DB8:0000:0000:0000:0000:0000:0001', '::ffff:192.0.2.1']:
        address = ipaddress.ip_address(text)
        canonical = str(address)
        # R always formats IPv6 with hexadecimal groups, including mapped IPv4.
        if address.version == 6 and address.ipv4_mapped is not None:
            tail = int(address.ipv4_mapped)
            canonical = f'::ffff:{tail >> 16:x}:{tail & 65535:x}'
        run('ip', text, expected=canonical + '\n')
    run('ip', '127.000.0.001', expected='127.0.0.1\n')
    for text in ['', '256.0.0.1', '1.2.3', '::1::', 'bad']:
        run('ip', text, status=65)
    for host, family in [('127.0.0.1', 'v4'), ('::1', 'v6'), ('localhost', 'any')]:
        result = run('resolve', host, 8080, family)
        rows = result.splitlines()
        assert rows and len(rows) == len(set(rows)), result
        for row in rows:
            fields = dict(item.split('=', 1) for item in row.split())
            address = ipaddress.ip_address(fields['address'])
            assert fields['port'] == '8080' and fields['scope'] == '0', row
            assert address.is_loopback, row
            assert family == 'any' or address.version == int(family[1]), row
    run('resolve', '127.0.0.1', 80, 'v6', status=69)
    run('resolve', 'bad name', 80, 'any', status=69)
    for message in ['', 'hello', 'caf\u00e9 \U0001f642', 'x' * 8192]:
        data = message.encode()
        # The scoped exchange and the one with owned buffers report the same receipt.
        for command in ['tcp', 'handoff']:
            run(command, message, expected=f'received={len(data)} crc32={zlib.crc32(data)} accepted=true endpoints_match=true\n')
        for capacity in sorted({0, 1, len(data) // 2, len(data), 60000}):
            prefix = data[:capacity]
            truncated = str(len(data) > capacity).lower()
            run('udp', message, capacity,
                expected=f'received={len(prefix)} crc32={zlib.crc32(prefix)} truncated={truncated} endpoints_match=true\n')
    long_message = 'x' * 60000
    for command in ['tcp', 'handoff']:
        run(command, long_message, expected=f'received=60000 crc32={zlib.crc32(long_message.encode())} accepted=true endpoints_match=true\n')
    for args in [('udp', 'message', 60001), ('tcp',), ('handoff', 'a', 'b'), ('bad', 'value'), ('resolve', 'localhost', 80, 'v3')]:
        run(*args, status=64)
    run('resolve', 'localhost', 65536, 'v4', status=65)
    # Socket options: the keepalive idle time rounds up to whole seconds and at least one.
    for milliseconds, seconds in [(0, 1), (1500, 2), (30000, 30)]:
        run('options', milliseconds, 64, expected=(
            f'tcp nodelay=true keepalive={seconds} hop_limit=64\n'
            'tcp keepalive=off\n'
            'udp broadcast=true multicast_hop_limit=3 multicast_loop=false\n'
            'multicast joined=239.1.2.3 repeated_refused=true unjoined_refused=true\n'))
    for hops in [0, 256]:
        run('options', 1000, hops, status=69)
    run('options', 3600001, 64, status=64)
    # Connection by name tries the resolver's addresses in order; a listed candidate that
    # refuses is skipped.
    for host in ['localhost', '127.0.0.1']:
        run('dial', host, expected='named connected matched=true\nlisted connected matched=true after_refusal=true\n')
    run('dial', 'bad name', status=69)
    print(f'Network laboratory: {count} address, resolver, TCP, datagram, option, dial and validation checks passed')


if __name__ == '__main__':
    main()
