#!/usr/bin/env python3
"""Differential tests of the deflate example against Python's zlib and gzip modules."""
import argparse
import random
import re
import struct
import subprocess
import zlib

SEED = 20260916
WBITS = {'rfc1951': -15, 'rfc1950': 15, 'rfc1952': 31}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    rng = random.Random(SEED)
    count = 0

    def run(args, data, status=0):
        nonlocal count
        result = subprocess.run([exe, *args], input=data, capture_output=True, timeout=120)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        if status == 0:
            assert not result.stderr, (args, result.stderr)
        count += 1
        return result

    words = [b'alpha', b'beta', b'gamma', b'delta', b'stream', b'window', b'huffman', b'block']
    text = b' '.join(rng.choice(words) for _ in range(2000))
    noise = bytes(rng.getrandbits(8) for _ in range(6000))
    pattern = bytes((index % 7) * 3 + index // 1000 for index in range(40000))
    corpus = [b'', b'a', b'abc', text, noise, pattern, text + noise[:2000] + text[:3000]]

    # Compression: every stream decodes with zlib to the original bytes.
    for data in corpus:
        for fmt, wbits in WBITS.items():
            for level in (0, 1, 6, 9):
                packed = run(['deflate', fmt, str(level)], data).stdout
                assert zlib.decompress(packed, wbits) == data, (fmt, level, len(data))
    # Small windows, tiny input pieces and a sync flush after every piece; the flushed
    # stream stays decodable and each flush ends on a byte boundary.
    for fmt, wbits in WBITS.items():
        for window, chunk in ((256, 1), (1024, 7), (32768, 1)):
            packed = run(['deflate', fmt, '9', str(window), str(chunk)], pattern[:9000]).stdout
            assert zlib.decompress(packed, wbits) == pattern[:9000], (fmt, window, chunk)
        for window, chunk in ((256, 512), (4096, 300), (32768, 4096)):
            packed = run(['deflate_sync', fmt, '6', str(window), str(chunk)], text).stdout
            assert zlib.decompress(packed, wbits) == text, (fmt, window, chunk)
    one_shot = run(['pack', 'rfc1950', '6'], text).stdout
    assert zlib.decompress(one_shot) == text
    assert run(['deflate', 'rfc1950', '6'], text).stdout == one_shot
    assert len(run(['deflate', 'rfc1951', '9'], pattern).stdout) * 20 < len(pattern)

    # Decompression: what zlib and gzip produce decodes here, in chunks of any size.
    for data in corpus:
        for fmt, wbits in WBITS.items():
            for level in (0, 1, 6, 9):
                packed = zlib.compress(data, level, wbits)
                assert run(['inflate', fmt], packed).stdout == data, (fmt, level, len(data))
                assert run(['unpack', fmt], packed).stdout == data
    for chunk in (1, 3, 13, 4096):
        packed = zlib.compress(text, 9, -15)
        assert run(['inflate', 'rfc1951', str(len(text) + 1), '32768', str(chunk)], packed).stdout == text
    for wbits in (9, 10, 12, 15):
        packed = zlib.compress(pattern, 6, wbits)
        window = 1 << wbits
        assert run(['inflate', 'rfc1950', str(len(pattern)), str(window), '997'], packed).stdout == pattern
    header = b'\x1f\x8b\x08' + bytes([0x1e]) + b'\0\0\0\0\x00\xff'
    extra = b'AB' + struct.pack('<H', 3) + b'xyz'
    header += struct.pack('<H', len(extra)) + extra + b'name.txt\0' + b'a comment\0' + b'\x12\x34'
    raw = zlib.compress(text, 6, -15)
    member = header + raw + struct.pack('<II', zlib.crc32(text), len(text))
    assert run(['inflate', 'rfc1952'], member).stdout == text
    stats = run(['stats', 'rfc1952', '1000000', '32768', '7'], member).stdout.decode()
    assert re.fullmatch(rf'consumed={len(member)} produced={len(text)} steps=\d+\n', stats), stats

    # Errors are checked errors with distinct names, never crashes.
    packed = zlib.compress(text)
    damaged = packed[:-1] + bytes([packed[-1] ^ 1])
    assert b'checksum_mismatch' in run(['inflate', 'rfc1950'], damaged, 65).stderr
    assert b'corrupt_stream' in run(['inflate', 'rfc1951'], b'\x07\x00', 65).stderr
    assert b'corrupt_stream' in run(['unpack', 'rfc1950'], packed[:8], 65).stderr
    assert b'output_limit' in run(['inflate', 'rfc1950', '10'], packed, 65).stderr
    assert b'trailing input' in run(['inflate', 'rfc1950'], packed + b'tail', 65).stderr
    assert b'unsupported' in run(['inflate', 'rfc1950'], b'\x78\xbb' + packed[2:], 65).stderr
    assert b'invalid_level' in run(['deflate', 'rfc1951', '10'], text, 65).stderr
    assert b'invalid_window' in run(['deflate', 'rfc1951', '6', '1000'], text, 65).stderr
    assert run(['deflate', 'rfc1953', '6'], text, 64).returncode == 64
    assert run(['squash', 'rfc1951', '6'], text, 64).returncode == 64
    assert run(['deflate', 'rfc1951'], text, 64).returncode == 64
    assert run(['inflate', 'rfc1951', 'many'], text, 64).returncode == 64
    print(f'deflate example: {count} command checks passed')


if __name__ == '__main__':
    main()
