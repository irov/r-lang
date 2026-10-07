#!/usr/bin/env python3
"""Check binary-tool output against independent hash and wire-format implementations."""
import argparse
import hashlib
import struct
import subprocess
import zlib


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True)
    exe = p.parse_args().executable
    count = 0

    def run(args, expected, status=0):
        nonlocal count
        result = subprocess.run([exe, *args], capture_output=True, text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (args, result)
        assert result.stdout == expected, (args, result.stdout, expected)
        count += 1

    for text in ['', 'abc', 'x'*55, 'x'*64, 'x'*128, 'caf\u00e9']:
        data = text.encode()
        expected = f'crc32 {zlib.crc32(data)}\n'
        for algorithm in ['md5', 'sha1', 'sha256', 'sha512']:
            expected += algorithm + ' ' + hashlib.new(algorithm, data).hexdigest() + '\n'
        run(['hash',text],expected)
    for text, sequence in [('',0),('hello',42),('caf\u00e9',2**64-1)]:
        data = text.encode()
        packet = struct.pack('<BHIQ',5,1,len(data),sequence)+data
        expected = f'urgent=1 encoding=2 version=1 length={len(data)} sequence={sequence}\n{packet.hex()}\n'
        run(['packet',text,str(sequence)],expected)
    for text in ['', 'sensor', 'abcdefghijklmnopq', '\u00e9'*10]:
        payload = text.encode()[:15]
        expected = f'copied={len(payload)} shifted=15\n' + (b'+'+payload.ljust(15,b' ')).hex()+'\n'
        run(['label',text],expected)
    run(['compare','ab\ncd','ab'],'equal=false order=1 prefix=true suffix=false utf8=true\npattern=0\nnewline=2\n')
    run(['compare','abc','bc'],'equal=false order=-1 prefix=false suffix=true utf8=true\npattern=1\nnewline=none\n')
    run(['compare','abc','abc'],'equal=true order=0 prefix=true suffix=true utf8=true\npattern=0\nnewline=none\n')
    run(['compare','abc','z'],'equal=false order=-1 prefix=false suffix=false utf8=true\npattern=none\nnewline=none\n')
    mask = 2**64 - 1
    for value in [0, 1, 2**63, mask, 0x0123456789abcdef, 0x8000000000000001, 1 << 40]:
        leading = 64 - value.bit_length()
        trailing = 64 if value == 0 else (value & -value).bit_length() - 1
        swapped = int.from_bytes(value.to_bytes(8, 'little'), 'big')
        left = ((value << 8) | (value >> 56)) & mask
        right = ((value >> 8) | (value << 56)) & mask
        run(['bits', str(value)],
            f'leading_zeros={leading} trailing_zeros={trailing} count_ones={bin(value).count("1")}\n'
            f'swap_bytes={swapped:016x} rotate_left={left:016x} rotate_right={right:016x}\n')
    for left, right in [(0, 0), (1, 0), (mask, mask), (mask, 2), (3, 5), (2**63, 2**63 + 7),
                        (0x0123456789abcdef, 0xfedcba9876543210)]:
        product = left * right
        total = left + right
        difference = (left - right) & mask
        expected = (f'product high={product >> 64:016x} low={product & mask:016x}\n'
                    f'sum={total & mask:016x} carry={"true" if total > mask else "false"}\n'
                    f'difference={difference:016x} borrow={"true" if left < right else "false"}\n')
        expected += f'quotient={left} remainder=0\n' if right else 'quotient none\n'
        run(['wide', str(left), str(right)], expected)
    run(['wide', '1'], 'wrong number of operands\n', 64)
    run(['nope','abc'],'unknown command\n',64)
    run(['packet','abc'],'wrong number of operands\n',64)
    print(f'Binary: {count} checksum, packet, label, comparison, bit, wide and input checks passed')


if __name__ == '__main__': main()
