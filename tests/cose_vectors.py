#!/usr/bin/env python3
"""Check std.cose against the COSE examples of the IETF COSE working group and RFC 9052.

The program under test reads one operation per line (see tests/fixtures/... cose driver) and
prints the hexadecimal result or `error CODE`. The vectors are the public-domain JSON files of
github.com/cose-wg/Examples, which the build downloads at a pinned commit, for the algorithms std.cose offers (EdDSA, HMAC, ChaCha20/Poly1305)
and their failure tests; messages of other algorithms, including the examples of RFC 9052
Appendix C, shall be read and refused as unsupported_algorithm.
"""
from __future__ import annotations

import argparse
import base64
import glob
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cbor_vectors import Map, Tag, encode  # noqa: E402

ALGORITHMS = {"EdDSA": -8, "HS256/64": 4, "HS256": 5, "HS384": 6, "HS512": 7, "ChaCha-Poly1305": 24,
              "ES256": -7, "A128GCM": 1}
LABELS = {"alg": 1, "ctyp": 3, "kid": 4}


def header_map(fields: dict | None) -> str:
    """The encoded header map of a vector, `-` when it has no parameters."""
    if not fields:
        return "-"
    entries = []
    for name, value in fields.items():
        label = LABELS[name]
        if name == "alg":
            value = ALGORITHMS[value]
        elif name == "kid":
            value = value.encode()
        entries.append((label, value))
    return encode(Map(entries)).hex()


def b64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def h(data: bytes) -> str:
    return data.hex() if data else "-"


# The directories of cose-wg/Examples whose messages use the algorithms of std.cose, and its
# failure tests.
DIRECTORIES = ("chacha-poly-examples", "eddsa-examples", "encrypted-tests", "hmac-examples",
               "mac0-tests", "sign1-tests")


def vectors(root: str):
    """(command, expected) pairs from the JSON vectors."""
    paths = [path for directory in DIRECTORIES
             for path in glob.glob(os.path.join(root, directory, "*.json"))]
    for path in sorted(paths):
        name = os.path.relpath(path, root)
        vector = json.load(open(path, encoding="utf-8"))
        fail = vector.get("fail", False)
        inputs = vector["input"]
        message = vector["output"]["cbor"].lower()
        plaintext = inputs["plaintext"].encode()
        if "sign0" in inputs:
            sign = inputs["sign0"]
            external = bytes.fromhex(sign.get("external", ""))
            key = sign["key"]
            if key.get("crv") == "Ed448":
                # Ed448 is not offered: its signature does not verify under any Ed25519 key.
                yield name, f"verify_sign1 {'00' * 32} {h(external)} {message}", None
            elif key.get("crv") == "Ed25519":
                public = key["x_hex"]
                yield name, f"verify_sign1 {public} {h(external)} {message}", (
                    "error verification_failed" if fail else plaintext.hex())
                if not fail:
                    command = (f"sign1 {key['d_hex']} {header_map(sign.get('protected'))} "
                               f"{header_map(sign.get('unprotected'))} {plaintext.hex()} {h(external)}")
                    yield name, command, message
            else:
                # ES256 is not offered: a well-formed message is refused by its algorithm.
                expected = "error unsupported_algorithm"
                if vector.get("input", {}).get("failures", {}).get("ChangeCBORTag"):
                    expected = "error wrong_type"
                yield name, f"verify_sign1 {'00' * 32} {h(external)} {message}", expected if not fail or "wrong_type" in expected else None
        elif "mac0" in inputs:
            mac = inputs["mac0"]
            external = bytes.fromhex(mac.get("external", ""))
            key = vector["intermediates"]["CEK_hex"].lower()
            failures = mac.get("failures", inputs.get("failures", {}))
            if fail:
                yield name, f"verify_mac0 {key} {h(external)} {message}", None
                continue
            yield name, f"verify_mac0 {key} {h(external)} {message}", plaintext.hex()
            if not message.startswith("d1") or failures:
                continue  # altered or untagged messages are read, not written
            command = (f"mac0 {key} {header_map(mac.get('protected'))} {header_map(mac.get('unprotected'))} "
                       f"{plaintext.hex()} {h(external)}")
            yield name, command, message
        elif "encrypted" in inputs:
            encrypted = inputs["encrypted"]
            external = bytes.fromhex(encrypted.get("external", ""))
            key = vector["intermediates"].get("CEK_hex", "00" * 32).lower()
            algorithm = encrypted.get("protected", {}).get("alg") or encrypted.get("unprotected", {}).get("alg")
            if algorithm == "ChaCha-Poly1305":
                yield name, f"decrypt0 {key} {h(external)} {message}", None if fail else plaintext.hex()
                if not fail:
                    iv = inputs["rng_stream"][0].lower()
                    command = (f"encrypt0 {key} {iv} {header_map(encrypted.get('protected'))} - "
                               f"{plaintext.hex()} {h(external)}")
                    yield name, command, message
            else:
                yield name, f"decrypt0 {key} {h(external)} {message}", None if fail else "error unsupported_algorithm"


def rfc9052_examples():
    """The single-recipient examples of RFC 9052 Appendix C with their stated sizes."""
    sign1 = Tag(18, [bytes.fromhex("a10126"), Map([(4, b"11")]), b"This is the content.",
                     bytes.fromhex("8eb33e4ca31d1c465ab05aac34cc6b23d58fef5c083106c4d25a91aef0b0117e2af9a291aa32e14ab834dc56ed2a223444547e01f11d3b0916e5a4c345cacb36")])
    encrypt0 = Tag(16, [bytes.fromhex("a1010a"), Map([(5, bytes.fromhex("89f52f65a1c580933b5261a78c"))]),
                        bytes.fromhex("5974e1b99a3a4cc09a659aa2e9e7fff161d38ce71cb45ce460ffb569")])
    partial = Tag(16, [bytes.fromhex("a1010a"), Map([(6, bytes.fromhex("61a7"))]),
                       bytes.fromhex("252a8911d465c125b6764739700f0141ed09192de139e053bd09abca")])
    mac0 = Tag(17, [bytes.fromhex("a1010f"), Map([]), b"This is the content.", bytes.fromhex("726043745027214f")])
    for title, item, size, command in (("C.2.1", sign1, 98, "verify_sign1 " + "00" * 32 + " -"),
                                       ("C.4.1", encrypt0, 52, "decrypt0 " + "00" * 32 + " -"),
                                       ("C.4.2", partial, 41, "decrypt0 " + "00" * 32 + " -"),
                                       ("C.6.1", mac0, 37, "verify_mac0 " + "00" * 32 + " -")):
        data = encode(item)
        assert len(data) == size, (title, len(data))
        yield f"RFC 9052 {title}", f"{command} {data.hex()}", "error unsupported_algorithm"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--vectors", required=True)
    arguments = parser.parse_args()
    cases = list(vectors(arguments.vectors)) + list(rfc9052_examples())
    run = subprocess.run([arguments.executable], input="\n".join(command for _, command, _ in cases) + "\n",
                         capture_output=True, text=True, timeout=300)
    if run.returncode != 0:
        print(run.stdout, run.stderr, file=sys.stderr)
        return 1
    outputs = run.stdout.rstrip("\n").split("\n")
    failures = 0
    for (name, command, expected), actual in zip(cases, outputs):
        ok = actual.startswith("error ") if expected is None else actual == expected
        if not ok:
            failures += 1
            print(f"FAIL {name}: {command[:50]}...\n  expected {expected}\n  actual   {actual[:120]}")
    if len(outputs) != len(cases):
        failures += 1
        print(f"FAIL {len(outputs)} outputs for {len(cases)} commands")
    print(f"cose vectors: {len(cases) - failures}/{len(cases)} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
