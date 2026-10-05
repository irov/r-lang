#!/usr/bin/env python3
"""Check std.jwt against an independent JWS made here with the Python `cryptography` package.

The program under test reads lines of tests/fixtures/codegen_jwt_driver.r. For every algorithm
of RFC 7518 and RFC 8037 that std.jwt implements, a token signed by the program verifies here,
a token signed here verifies there with the key alone and through a JWK Set written here, a
changed token is refused, and the JWK that the program writes for a key holds the members of
RFC 7518 section 6 that are written here.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import os
import subprocess
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, padding, rsa
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature

HASHES = {"256": (hashes.SHA256(), hashlib.sha256), "384": (hashes.SHA384(), hashlib.sha384),
          "512": (hashes.SHA512(), hashlib.sha512)}


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def der_private(key) -> bytes:
    return key.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption())


def der_public(key) -> bytes:
    return key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)


def make_key(alg: str):
    """The private key (or secret) of an algorithm and the hex of what the program signs with
    and verifies with."""
    if alg.startswith("HS"):
        secret = os.urandom(int(alg[2:]) // 8 + 5)
        return secret, secret.hex(), secret.hex()
    if alg in ("RS256", "RS384", "RS512", "PS256", "PS384", "PS512"):
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    elif alg == "ES256":
        key = ec.generate_private_key(ec.SECP256R1())
    elif alg == "ES384":
        key = ec.generate_private_key(ec.SECP384R1())
    else:
        key = ed25519.Ed25519PrivateKey.generate()
    return key, der_private(key).hex(), der_public(key.public_key()).hex()


def sign(alg: str, key, data: bytes) -> bytes:
    if alg.startswith("HS"):
        return hmac.new(key, data, HASHES[alg[2:]][1]).digest()
    if alg.startswith("RS"):
        return key.sign(data, padding.PKCS1v15(), HASHES[alg[2:]][0])
    if alg.startswith("PS"):
        digest = HASHES[alg[2:]][0]
        return key.sign(data, padding.PSS(padding.MGF1(digest), digest.digest_size), digest)
    if alg.startswith("ES"):
        size = 32 if alg == "ES256" else 48
        r, s = decode_dss_signature(key.sign(data, ec.ECDSA(HASHES[alg[2:]][0])))
        return r.to_bytes(size, "big") + s.to_bytes(size, "big")
    return key.sign(data)


def verifies(alg: str, key, data: bytes, signature: bytes) -> bool:
    try:
        if alg.startswith("HS"):
            return hmac.compare_digest(sign(alg, key, data), signature)
        public = key.public_key()
        if alg.startswith("RS"):
            public.verify(signature, data, padding.PKCS1v15(), HASHES[alg[2:]][0])
        elif alg.startswith("PS"):
            digest = HASHES[alg[2:]][0]
            public.verify(signature, data, padding.PSS(padding.MGF1(digest), digest.digest_size), digest)
        elif alg.startswith("ES"):
            size = len(signature) // 2
            der = encode_dss_signature(int.from_bytes(signature[:size], "big"), int.from_bytes(signature[size:], "big"))
            public.verify(der, data, ec.ECDSA(HASHES[alg[2:]][0]))
        else:
            public.verify(signature, data)
    except InvalidSignature:
        return False
    return True


def token(alg: str, key, claims: dict, kid: str | None = None) -> str:
    header = {"alg": alg, "typ": "JWT"}
    if kid is not None:
        header["kid"] = kid
    signing_input = b64(json.dumps(header, separators=(",", ":")).encode()) + "." + \
        b64(json.dumps(claims, separators=(",", ":")).encode())
    return signing_input + "." + b64(sign(alg, key, signing_input.encode()))


def jwk(alg: str, key, kid: str) -> dict:
    if alg.startswith("HS"):
        return {"kty": "oct", "k": b64(key), "alg": alg, "kid": kid}
    public = key.public_key()
    if isinstance(public, rsa.RSAPublicKey):
        numbers = public.public_numbers()
        return {"kty": "RSA", "n": b64(numbers.n.to_bytes((numbers.n.bit_length() + 7) // 8, "big")),
                "e": b64(numbers.e.to_bytes((numbers.e.bit_length() + 7) // 8, "big")), "alg": alg, "kid": kid,
                "use": "sig"}
    if isinstance(public, ec.EllipticCurvePublicKey):
        size = 32 if alg == "ES256" else 48
        numbers = public.public_numbers()
        return {"kty": "EC", "crv": "P-256" if size == 32 else "P-384", "x": b64(numbers.x.to_bytes(size, "big")),
                "y": b64(numbers.y.to_bytes(size, "big")), "alg": alg, "kid": kid, "use": "sig"}
    raw = public.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return {"kty": "OKP", "crv": "Ed25519", "x": b64(raw), "alg": alg, "kid": kid, "use": "sig"}


def checks_program_token(alg: str, key, claims: dict):
    def check(actual: str) -> bool:
        parts = actual.split(".")
        if len(parts) != 3:
            return False
        header = json.loads(unb64(parts[0]))
        if header.get("alg") != alg or header.get("typ") != "JWT":
            return False
        if json.loads(unb64(parts[1])) != claims:
            return False
        return verifies(alg, key, (parts[0] + "." + parts[1]).encode(), unb64(parts[2]))
    return check


def checks_claims(claims: dict):
    return lambda actual: not actual.startswith("error") and json.loads(actual) == claims


def checks_jwk(expected: dict):
    return lambda actual: json.loads(actual) == expected


def cases():
    now = 1_700_000_000
    for alg in ("HS256", "HS384", "HS512", "RS256", "RS384", "RS512", "PS256", "PS384", "PS512",
                "ES256", "ES384", "EdDSA"):
        key, signing_hex, verifying_hex = make_key(alg)
        claims = {"sub": "player-" + alg, "AccountID": "42", "admin": False, "level": 7,
                  "exp": now + 600, "nbf": now - 600}
        claims_hex = json.dumps(claims, separators=(",", ":")).encode().hex()
        yield f"sign {alg} {signing_hex} {claims_hex}", checks_program_token(alg, key, claims)
        made = token(alg, key, claims)
        yield f"verify {alg} {verifying_hex} {made} {now}", checks_claims(claims)
        yield f"verify {alg} {verifying_hex} {made} {now + 3600}", "error expired"
        header, payload, signature = made.split(".")
        changed = b64(json.dumps(dict(claims, admin=True), separators=(",", ":")).encode())
        yield f"verify {alg} {verifying_hex} {header}.{changed}.{signature} {now}", "error invalid_signature"
        set_hex = json.dumps({"keys": [jwk(alg, key, "k-" + alg)]}).encode().hex()
        yield f"jwks {set_hex} {token(alg, key, claims, 'k-' + alg)} {now}", checks_claims(claims)
        yield f"jwks {set_hex} {token(alg, key, claims, 'other')} {now}", "error no_key"
        if not alg.startswith("HS"):
            yield f"jwk {alg} {verifying_hex} k-{alg}", checks_jwk(jwk(alg, key, "k-" + alg))
    # An algorithm the key does not fit, `alg` none and a header that requires an extension.
    key, signing_hex, verifying_hex = make_key("ES256")
    other, _, other_hex = make_key("ES384")
    yield f"verify ES384 {other_hex} {token('ES256', key, {'a': 1})} {now}", "error no_key"
    yield f"sign ES384 {signing_hex} 7b7d", "error key_mismatch"
    none_token = b64(b'{"alg":"none"}') + "." + b64(b'{"a":1}') + "."
    yield f"verify ES256 {verifying_hex} {none_token} {now}", "error unsupported_algorithm"
    critical = b64(b'{"alg":"ES256","crit":["exp"]}') + "." + b64(b'{"a":1}')
    critical_token = critical + "." + b64(sign("ES256", key, critical.encode()))
    yield f"verify ES256 {verifying_hex} {critical_token} {now}", "error unsupported_algorithm"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    pairs = list(cases())
    run = subprocess.run([arguments.executable], input="\n".join(command for command, _ in pairs) + "\n",
                         capture_output=True, text=True, timeout=300)
    if run.returncode != 0:
        print(run.stdout, run.stderr, file=sys.stderr)
        return 1
    outputs = run.stdout.rstrip("\n").split("\n")
    failures = 0
    for (command, expected), actual in zip(pairs, outputs):
        passed = expected(actual) if callable(expected) else actual == expected
        if not passed:
            failures += 1
            shown = "a value its check accepts" if callable(expected) else expected
            print(f"FAIL {command[:70]}...\n  expected {shown}\n  actual   {actual[:200]}")
    if len(outputs) != len(pairs):
        failures += 1
        print(f"FAIL {len(outputs)} outputs for {len(pairs)} commands")
    print(f"jwt vectors: {len(pairs) - failures}/{len(pairs)} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
