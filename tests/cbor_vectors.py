#!/usr/bin/env python3
"""Check std.cbor against the examples of RFC 8949 Appendix A and malformed inputs.

The program under test reads one hexadecimal item per line and prints its diagnostic
notation, its deterministic encoding and whether the input already was deterministic, or
`error CODE OFFSET`. This harness carries an independent decoder and deterministic encoder
(RFC 8949 sections 3 and 4.2) as the oracle, and also compares the diagnostic notation with
the one printed in the RFC.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
import subprocess
import sys

INDEFINITE = object()


class Tag:
    def __init__(self, number, content):
        self.number, self.content = number, content


class Simple:
    def __init__(self, number):
        self.number = number


class Undefined:
    pass


class Map(list):
    """The entries of a map as (key, value) pairs, apart from an array of pairs."""


class Malformed(Exception):
    pass


def decode(data: bytes):
    item, at = _item(data, 0)
    if at != len(data):
        raise Malformed("trailing")
    return item


def _argument(data, at, info):
    if info < 24:
        return info, at
    size = {24: 1, 25: 2, 26: 4, 27: 8}.get(info)
    if size is None or at + size > len(data):
        raise Malformed("argument")
    return int.from_bytes(data[at:at + size], "big"), at + size


def _item(data, at):
    if at >= len(data):
        raise Malformed("truncated")
    initial = data[at]
    major, info = initial >> 5, initial & 31
    at += 1
    if major in (2, 3) and info == 31:
        chunks = b""
        while True:
            if at >= len(data):
                raise Malformed("truncated")
            if data[at] == 0xFF:
                at += 1
                break
            if data[at] >> 5 != major or data[at] & 31 == 31:
                raise Malformed("chunk")
            length, at = _argument(data, at + 1, data[at] & 31)
            chunks += data[at:at + length]
            at += length
        return (chunks if major == 2 else chunks.decode("utf-8")), at
    if major in (4, 5) and info == 31:
        items = []
        while True:
            if at >= len(data):
                raise Malformed("truncated")
            if data[at] == 0xFF:
                at += 1
                break
            element, at = _item(data, at)
            items.append(element)
        return (items if major == 4 else _pairs(items)), at
    if major == 7:
        if info == 20:
            return False, at
        if info == 21:
            return True, at
        if info == 22:
            return None, at
        if info == 23:
            return Undefined(), at
        if info < 24:
            return Simple(info), at
        if info == 24:
            return Simple(data[at]), at + 1
        if info == 25:
            return struct.unpack(">e", data[at:at + 2])[0], at + 2
        if info == 26:
            return struct.unpack(">f", data[at:at + 4])[0], at + 4
        if info == 27:
            return struct.unpack(">d", data[at:at + 8])[0], at + 8
        raise Malformed("special")
    argument, at = _argument(data, at, info)
    if major == 0:
        return argument, at
    if major == 1:
        return -1 - argument, at
    if major in (2, 3):
        chunk = data[at:at + argument]
        if len(chunk) != argument:
            raise Malformed("truncated")
        return (chunk if major == 2 else chunk.decode("utf-8")), at + argument
    if major == 4:
        items = []
        for _ in range(argument):
            element, at = _item(data, at)
            items.append(element)
        return items, at
    if major == 5:
        items = []
        for _ in range(2 * argument):
            element, at = _item(data, at)
            items.append(element)
        return _pairs(items), at
    content, at = _item(data, at)
    return Tag(argument, content), at


def _pairs(items):
    return Map((items[index], items[index + 1]) for index in range(0, len(items), 2))


def _head(major, argument):
    if argument < 24:
        return bytes([major << 5 | argument])
    for info, size in ((24, 1), (25, 2), (26, 4), (27, 8)):
        if argument < 1 << (8 * size):
            return bytes([major << 5 | info]) + argument.to_bytes(size, "big")
    raise ValueError(argument)


def encode(item) -> bytes:
    """Core deterministic encoding (RFC 8949 section 4.2.1)."""
    if item is False:
        return b"\xf4"
    if item is True:
        return b"\xf5"
    if item is None:
        return b"\xf6"
    if isinstance(item, Undefined):
        return b"\xf7"
    if isinstance(item, Simple):
        return bytes([0xE0 | item.number]) if item.number < 24 else bytes([0xF8, item.number])
    if isinstance(item, int):
        return _head(0, item) if item >= 0 else _head(1, -1 - item)
    if isinstance(item, float):
        if math.isnan(item):
            return b"\xf9\x7e\x00"
        for code, form in ((0xF9, ">e"), (0xFA, ">f"), (0xFB, ">d")):
            try:
                packed = struct.pack(form, item)
            except OverflowError:
                continue
            if struct.unpack(form, packed)[0] == item:
                return bytes([code]) + packed
    if isinstance(item, bytes):
        return _head(2, len(item)) + item
    if isinstance(item, str):
        raw = item.encode("utf-8")
        return _head(3, len(raw)) + raw
    if isinstance(item, Map):
        pairs = sorted((encode(key), encode(element)) for key, element in item)
        return _head(5, len(pairs)) + b"".join(key + element for key, element in pairs)
    if isinstance(item, list):
        return _head(4, len(item)) + b"".join(encode(element) for element in item)
    if isinstance(item, Tag):
        return _head(6, item.number) + encode(item.content)
    raise TypeError(item)


FLOAT_TOKEN = re.compile(r"-?(?:\d+\.\d+(?:e[+-]?\d+)?|\d+e[+-]?\d+|Infinity|NaN)")


def same_diagnostic(actual: str, expected: str) -> bool:
    """Equal up to the spelling of floats, which are compared as numbers."""
    actual_parts, expected_parts = FLOAT_TOKEN.split(actual), FLOAT_TOKEN.split(expected)
    actual_floats, expected_floats = FLOAT_TOKEN.findall(actual), FLOAT_TOKEN.findall(expected)
    if actual_parts != expected_parts or len(actual_floats) != len(expected_floats):
        return False
    for left, right in zip(actual_floats, expected_floats):
        a, b = float(left), float(right)
        if not (a == b and math.copysign(1, a) == math.copysign(1, b)) and not (math.isnan(a) and math.isnan(b)):
            return False
    return True


def rfc_expected(diagnostic: str) -> str:
    """The RFC diagnostic in the notation of std.cbor: bignums as tagged byte strings, no
    indefinite-length markers, and the ASCII escapes of the RFC decoded."""
    bignums = {"18446744073709551616": "2(h'010000000000000000')",
               "-18446744073709551617": "3(h'010000000000000000')"}
    if diagnostic in bignums:
        return bignums[diagnostic]
    if diagnostic == "(_ h'0102', h'030405')":
        return "h'0102030405'"
    if diagnostic == '(_ "strea", "ming")':
        return '"streaming"'
    text = diagnostic.replace("[_ ", "[").replace("{_ ", "{")
    units = re.sub(r"\\u([0-9a-f]{4})", lambda match: chr(int(match.group(1), 16)), text)
    return units.encode("utf-16", "surrogatepass").decode("utf-16")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--vectors", required=True)
    arguments = parser.parse_args()
    vectors = json.load(open(arguments.vectors, encoding="utf-8"))
    malformed = [
        ("", "truncated"), ("18", "truncated"), ("1c", "malformed"), ("5f00ff", "malformed"),
        ("ff", "malformed"), ("8301", "truncated"), ("a1", "truncated"), ("0000", "trailing_data"),
        ("62c328", "invalid_utf8"), ("a201020103", "duplicate_key"), ("f818", "malformed"),
        ("7f61e0ff", "invalid_utf8"), ("9f01", "truncated"), ("bf01ff", "malformed"),
        ("c0", "truncated"), ("1f", "malformed"), ("5b00000000ffffffff", "truncated"),
        ("81" * 1100 + "00", "too_deep"),
    ]
    not_deterministic = ["1817", "190017", "fb3ff0000000000000", "fa3f800000", "5f40ff", "9fff", "a203010202",
                         "fa7fc00000", "bf01f6ff"]
    lines = [vector["hex"] for vector in vectors] + [case for case, _ in malformed] + not_deterministic
    completed = subprocess.run([arguments.executable], input="\n".join(lines) + "\n",
                               capture_output=True, text=True, timeout=120)
    if completed.returncode != 0:
        print(completed.stderr, file=sys.stderr)
        return 1
    outputs = completed.stdout.rstrip("\n").split("\n")
    failures = 0
    checks = 0

    def check(condition, message):
        nonlocal failures, checks
        checks += 1
        if not condition:
            failures += 1
            print("FAIL", message)

    for vector, output in zip(vectors, outputs):
        fields = output.split("\t")
        data = bytes.fromhex(vector["hex"])
        oracle = decode(data)
        check(len(fields) == 3, f"{vector['hex']}: {output}")
        if len(fields) != 3:
            continue
        shown, encoded, strict = fields
        check(same_diagnostic(shown, rfc_expected(vector["diagnostic"])),
              f"{vector['hex']}: diagnostic {shown!r} != {rfc_expected(vector['diagnostic'])!r}")
        check(encoded == encode(oracle).hex(), f"{vector['hex']}: encoded {encoded} != {encode(oracle).hex()}")
        expected_strict = "deterministic" if encode(oracle) == data else "not-deterministic"
        check(strict == expected_strict, f"{vector['hex']}: {strict} != {expected_strict}")
    offset = len(vectors)
    for (case, code), output in zip(malformed, outputs[offset:]):
        fields = output.split("\t")
        check(fields[0] == "error" and fields[1] == code, f"malformed {case[:24]}: {output[:80]} != {code}")
    offset += len(malformed)
    for case, output in zip(not_deterministic, outputs[offset:]):
        check(output.endswith("\tnot-deterministic"), f"strict {case}: {output}")
    check(len(outputs) == len(lines), f"{len(outputs)} outputs for {len(lines)} inputs")
    print(f"cbor vectors: {checks - failures}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
