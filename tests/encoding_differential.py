"""Deterministic differential test of std.encoding (M19) through executable R code: base64,
base64url, hexadecimal and percent-encoding against the Python standard library, and the
canonical decoding errors of R-SLIB-ENCODING-0002..0004 with their byte indices."""

from __future__ import annotations

import argparse
import base64
import binascii
import random
import subprocess
import urllib.parse


SEED = 20260929
STANDARD = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
URL = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
HEX = b"0123456789abcdefABCDEF"


def listing(data: bytes) -> str:
    return " ".join(["bytes"] + [str(value) for value in data])


def failure(code: int, index: int) -> str:
    return f"error {code} {index}"


def decode_base64(text: str, url: bool) -> str:
    source = text.encode()
    end = len(source)
    padding = 0
    while end > 0 and padding < 2 and source[end - 1] == ord("="):
        end -= 1
        padding += 1
    alphabet = URL if url else STANDARD
    for index in range(end):
        if source[index] not in alphabet:
            return failure(0, index)
    rest = end % 4
    if rest == 1 or ((not url or padding) and len(source) % 4 != 0):
        return failure(1, end - rest)
    body = source[:end] + b"=" * ((4 - rest) % 4)
    data = base64.b64decode(body, altchars=b"-_" if url else None, validate=True)
    # Canonical text only: the unused bits of the last symbol are zero exactly when encoding the
    # bytes again gives the same symbols.
    again = base64.b64encode(data, altchars=b"-_" if url else None).rstrip(b"=")
    if again != source[:end]:
        return failure(0, end - 1)
    return listing(data)


def decode_hex(text: str) -> str:
    source = text.encode()
    for index, value in enumerate(source):
        if value not in HEX:
            return failure(0, index)
    if len(source) % 2 != 0:
        return failure(1, len(source) - 1)
    return listing(binascii.unhexlify(source))


def percent_decode(text: str) -> str:
    source = text.encode()
    result = bytearray()
    index = 0
    while index < len(source):
        if source[index] != ord("%"):
            result.append(source[index])
            index += 1
            continue
        pair = source[index + 1 : index + 3]
        if len(pair) != 2 or pair[0] not in HEX or pair[1] not in HEX:
            return failure(0, index)
        result.append(int(pair, 16))
        index += 3
    return listing(bytes(result))


def corrupt(rng: random.Random, text: str, symbols: str) -> str:
    choice = rng.randrange(4)
    if choice == 0 and text:
        at = rng.randrange(len(text))
        return text[:at] + rng.choice(symbols) + text[at + 1 :]
    if choice == 1 and text:
        return text[: rng.randrange(len(text))]
    if choice == 2:
        at = rng.randrange(len(text) + 1)
        return text[:at] + rng.choice(symbols) + text[at:]
    return text + rng.choice(symbols)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    rng = random.Random(SEED)
    cases: list[tuple[str, str, str]] = []
    for _ in range(160):
        data = bytes(rng.randrange(256) for _ in range(rng.randrange(49)))
        numbers = " ".join(str(value) for value in data)
        cases.append(("b64", numbers, base64.b64encode(data).decode()))
        cases.append(("b64url", numbers, base64.urlsafe_b64encode(data).rstrip(b"=").decode()))
        cases.append(("hex", numbers, data.hex()))
        standard = base64.b64encode(data).decode()
        url = base64.urlsafe_b64encode(data).decode()
        cases.append(("d64", standard, listing(data)))
        cases.append(("d64url", url, listing(data)))
        cases.append(("d64url", url.rstrip("="), listing(data)))
        noisy = corrupt(rng, standard, "=A/+-_$é")
        cases.append(("d64", noisy, decode_base64(noisy, False)))
        noisy = corrupt(rng, url.rstrip("=") if rng.randrange(2) else url, "=A/+-_$é")
        cases.append(("d64url", noisy, decode_base64(noisy, True)))
        mixed = "".join(c.upper() if rng.randrange(2) else c for c in data.hex())
        cases.append(("dhex", mixed, listing(data)))
        noisy = corrupt(rng, mixed, "0aFgx é")
        cases.append(("dhex", noisy, decode_hex(noisy)))
        text = "".join(rng.choice("aZ09-._~ /%?#&=é€😀\n+") for _ in range(rng.randrange(12)))
        quoted = urllib.parse.quote(text, safe="")
        cases.append(("pct", text, quoted))
        cases.append(("dpct", quoted, listing(text.encode())))
        lowered = quoted.lower() if rng.randrange(2) else quoted
        cases.append(("dpct", lowered, percent_decode(lowered)))
        noisy = corrupt(rng, quoted, "%%4fZé")
        cases.append(("dpct", noisy, percent_decode(noisy)))
    command = [arguments.executable]
    for mode, argument, _expected in cases:
        command.extend([mode, argument])
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode != 0 or result.stderr:
        raise AssertionError((result.returncode, result.stderr))
    lines = result.stdout.split("\n")
    assert lines[-1] == "" and len(lines) == len(cases) + 1, (len(lines), len(cases))
    for (mode, argument, expected), actual in zip(cases, lines):
        assert actual == expected, (SEED, mode, argument, expected, actual)
    print(f"encoding differential cases checked: {len(cases)}")


if __name__ == "__main__":
    main()
