#!/usr/bin/env python3
"""Check std.crypto against published vectors and against the Python `cryptography` package.

The program under test reads `OPERATION ARG...` lines (hexadecimal arguments, `-` for empty,
decimal numbers) and prints the hexadecimal result or `error CODE`. Known-answer vectors come
from RFC 8032 (Ed25519), RFC 7748 (X25519), RFC 8439 (ChaCha20-Poly1305), RFC 5869 (HKDF) and
RFC 7693 (BLAKE2b); random cases are checked against the independent implementations of
`cryptography` and `hashlib`. The public-key cases (M36) compare ECDSA with the deterministic
signatures of RFC 6979 that `cryptography` makes, RSA PKCS#1 v1.5 signatures byte for byte, PSS and
generated keys by verification, AES-CBC with PKCS#7 byte for byte, and keys read from PKCS#8,
SEC1, PKCS#1 and SPKI in DER and PEM by the exact PKCS#8 and SPKI that `cryptography` writes.
An expected value that is a function checks an output that is random by nature.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import random
import subprocess
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import padding as block_padding
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import dsa, ec, ed25519, rsa, x25519
from cryptography.hazmat.primitives.asymmetric import padding as rsa_padding
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

try:
    from cryptography.hazmat.primitives.kdf.argon2 import Argon2id
except ImportError:  # pragma: no cover - older cryptography
    Argon2id = None


def h(data: bytes) -> str:
    return data.hex() if data else "-"


def raw_public(key) -> bytes:
    return key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)


def cases(rng: random.Random):
    """(command, expected output) pairs."""
    # RFC 8032 section 7.1, tests 1 to 3.
    for seed, public, message, signature in (
        ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
        ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
        ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"),
    ):
        yield f"sign {seed} {message or '-'}", f"{public} {signature}"
        yield f"verify {public} {message or '-'} {signature}", "true"
        tampered = signature[:-2] + ("00" if signature[-2:] != "00" else "01")
        yield f"verify {public} {message or '-'} {tampered}", "false"
    yield "verify 00 - 00", "false"
    # RFC 7748 section 6.1: Alice and Bob.
    alice = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"
    alice_public = "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"
    bob = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb"
    bob_public = "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"
    shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"
    yield f"x25519 {alice} {bob_public}", f"{alice_public} {shared}"
    yield f"x25519 {bob} {alice_public}", f"{bob_public} {shared}"
    yield f"x25519 {alice} {'00' * 32}", "error weak_key"
    yield f"x25519 {alice} 00", "error invalid_length"
    # RFC 8439 section 2.8.2.
    plaintext = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
    key = "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
    nonce = "070000004041424344454647"
    aad = "50515253c0c1c2c3c4c5c6c7"
    sealed = ("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b"
              "1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
              "3ff4def08e4b7a9de576d26586cec64b6116" "1ae10b594f09e26a7e902ecbd0600691")
    yield f"seal chacha20_poly1305 {key} {nonce} {aad} {plaintext.hex()}", sealed
    yield f"open chacha20_poly1305 {key} {nonce} {aad} {sealed}", plaintext.hex()
    yield f"open chacha20_poly1305 {key} {nonce} {aad} {sealed[:-2]}00", "error authentication_failed"
    yield f"open chacha20_poly1305 {key} {nonce} - {sealed}", "error authentication_failed"
    yield f"seal chacha20_poly1305 {key} 00 - 00", "error invalid_length"
    # RFC 5869 appendix A, test cases 1 to 3 (SHA-256).
    yield ("hkdf256 000102030405060708090a0b0c 0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b f0f1f2f3f4f5f6f7f8f9 42",
           "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865")
    yield ("hkdf256 606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf "
           "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f "
           "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff 82",
           "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87")
    yield ("hkdf256 - 0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b - 42",
           "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8")
    yield "hkdf256 - 00 - 8161", "error invalid_length"
    # RFC 7693 appendix A: BLAKE2b-512 of "abc".
    yield ("blake2b 616263 - 64",
           "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923")
    yield "blake2b 616263 - 8", "error invalid_length"
    # Random cases against the independent implementations.
    for _ in range(12):
        seed = os.urandom(32)
        message = os.urandom(rng.randrange(0, 200))
        key = ed25519.Ed25519PrivateKey.from_private_bytes(seed)
        yield f"sign {h(seed)} {h(message)}", f"{raw_public(key).hex()} {key.sign(message).hex()}"
        secret, peer = x25519.X25519PrivateKey.generate(), x25519.X25519PrivateKey.generate()
        secret_raw = secret.private_bytes_raw()
        yield (f"x25519 {secret_raw.hex()} {raw_public(peer).hex()}",
               f"{raw_public(secret).hex()} {secret.exchange(peer.public_key()).hex()}")
        aead_key, aead_nonce = os.urandom(32), os.urandom(12)
        aad_bytes, text = os.urandom(rng.randrange(0, 40)), os.urandom(rng.randrange(0, 300))
        yield (f"seal chacha20_poly1305 {h(aead_key)} {h(aead_nonce)} {h(aad_bytes)} {h(text)}",
               ChaCha20Poly1305(aead_key).encrypt(aead_nonce, text, aad_bytes).hex())
        salt, material, info = os.urandom(rng.randrange(0, 40)), os.urandom(rng.randrange(1, 80)), os.urandom(rng.randrange(0, 40))
        length = rng.randrange(1, 200)
        for name, algorithm in (("hkdf256", hashes.SHA256()), ("hkdf512", hashes.SHA512())):
            derived = HKDF(algorithm=algorithm, length=length, salt=salt or None, info=info).derive(material)
            yield f"{name} {h(salt)} {h(material)} {h(info)} {length}", derived.hex()
        data, mac_key = os.urandom(rng.randrange(0, 300)), os.urandom(rng.randrange(0, 65))
        digest_length = rng.randrange(16, 65)
        expected = hashlib.blake2b(data, key=mac_key, digest_size=digest_length).hexdigest()
        yield f"blake2b {h(data)} {h(mac_key)} {digest_length}", expected
        yield f"blake2b_stream {h(data)} {h(mac_key)} {digest_length}", expected
    # XChaCha20-Poly1305 round trip (no independent implementation in `cryptography`).
    xkey, xnonce = os.urandom(32), os.urandom(24)
    yield f"open xchacha20_poly1305 {h(xkey)} {h(xnonce)} - 00", "error authentication_failed"
    if Argon2id is not None:
        for operations, memory in ((1, 8192), (2, 16384)):
            password, salt = os.urandom(rng.randrange(1, 40)), os.urandom(16)
            derived = Argon2id(salt=salt, length=32, iterations=operations, lanes=1,
                               memory_cost=memory // 1024).derive(password)
            yield f"argon2id {h(password)} {h(salt)} {operations} {memory} 32", derived.hex()
    yield "argon2id 70 00 1 8192 32", "error invalid_length"
    yield "password 70617373776f7264", "true false"


CURVES = {"p256": (ec.SECP256R1(), hashes.SHA256(), 32), "p384": (ec.SECP384R1(), hashes.SHA384(), 48)}
SCHEMES = {
    "pkcs1_sha256": (False, hashes.SHA256()), "pkcs1_sha384": (False, hashes.SHA384()),
    "pkcs1_sha512": (False, hashes.SHA512()), "pss_sha256": (True, hashes.SHA256()),
    "pss_sha384": (True, hashes.SHA384()), "pss_sha512": (True, hashes.SHA512()),
}


def pkcs8_der(key) -> bytes:
    return key.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption())


def pkcs8_pem(key) -> bytes:
    return key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption())


def spki_der(key) -> bytes:
    return key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)


def spki_pem(key) -> bytes:
    return key.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)


def raw_signature(der: bytes, size: int) -> bytes:
    r, s = decode_dss_signature(der)
    return r.to_bytes(size, "big") + s.to_bytes(size, "big")


def rsa_sign(key, scheme: str, message: bytes) -> bytes:
    pss, digest = SCHEMES[scheme]
    if pss:
        return key.sign(message, rsa_padding.PSS(rsa_padding.MGF1(digest), digest.digest_size), digest)
    return key.sign(message, rsa_padding.PKCS1v15(), digest)


def rsa_verifies(public, scheme: str, message: bytes, signature: bytes) -> bool:
    pss, digest = SCHEMES[scheme]
    try:
        if pss:
            public.verify(signature, message, rsa_padding.PSS(rsa_padding.MGF1(digest), digest.digest_size), digest)
        else:
            public.verify(signature, message, rsa_padding.PKCS1v15(), digest)
    except InvalidSignature:
        return False
    return True


def checks_generated(kind: str, scheme_or_curve: str, message: bytes, extra: str):
    """A check of `ecdsa_generate` or `rsa_generate`: the keys read back, the signature verifies
    with `cryptography`, and the trailing fields are as stated."""
    def check(actual: str) -> bool:
        parts = actual.split(" ")
        if len(parts) < 4 or " ".join(parts[3:]) != extra:
            return False
        private = serialization.load_der_private_key(bytes.fromhex(parts[0]), None)
        if spki_der(private.public_key()) != bytes.fromhex(parts[1]) or pkcs8_der(private) != bytes.fromhex(parts[0]):
            return False
        signature = bytes.fromhex(parts[2])
        if kind == "ecdsa":
            _, digest, size = CURVES[scheme_or_curve]
            der = encode_dss_signature(int.from_bytes(signature[:size], "big"), int.from_bytes(signature[size:], "big"))
            try:
                private.public_key().verify(der, message, ec.ECDSA(digest))
            except InvalidSignature:
                return False
            return True
        return rsa_verifies(private.public_key(), scheme_or_curve, message, signature)
    return check


def checks_rsa_signature(public, scheme: str, message: bytes):
    return lambda actual: rsa_verifies(public, scheme, message, bytes.fromhex(actual))


def cbc_encrypt(key: bytes, iv: bytes, plaintext: bytes) -> bytes:
    padder = block_padding.PKCS7(128).padder()
    data = padder.update(plaintext) + padder.finalize()
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return encryptor.update(data) + encryptor.finalize()


def public_key_cases(rng: random.Random):
    """(command, expected output or check) pairs of the M36 primitives and key formats."""
    for name, (curve, digest, size) in CURVES.items():
        order = {"p256": 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551,
                 "p384": int("ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973", 16)}[name]
        generator = ec.derive_private_key(1, curve).public_key().public_bytes(
            serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        yield f"ecdsa_public {name} 01", generator.hex()
        yield f"ecdsa_public {name} {'00' * size}", "error invalid_key"
        yield f"ecdsa_public {name} {order.to_bytes(size, 'big').hex()}", "error invalid_key"
        yield f"ecdsa_public {name} {'01' * (size + 1)}", "error invalid_key"
        for _ in range(6):
            key = ec.generate_private_key(curve)
            scalar = key.private_numbers().private_value.to_bytes(size, "big")
            point = key.public_key().public_bytes(serialization.Encoding.X962,
                                                  serialization.PublicFormat.UncompressedPoint)
            message = os.urandom(rng.randrange(0, 120))
            yield f"ecdsa_public {name} {scalar.hex()}", point.hex()
            deterministic = key.sign(message, ec.ECDSA(digest, deterministic_signing=True))
            yield f"ecdsa_sign {name} {scalar.hex()} {h(message)}", raw_signature(deterministic, size).hex()
            randomized = raw_signature(key.sign(message, ec.ECDSA(digest)), size)
            yield f"ecdsa_verify {name} {point.hex()} {h(message)} {randomized.hex()}", "true"
            tampered = bytes([randomized[0] ^ 1]) + randomized[1:]
            yield f"ecdsa_verify {name} {point.hex()} {h(message)} {tampered.hex()}", "false"
            yield f"ecdsa_verify {name} {point.hex()} {h(message)} {randomized[:-1].hex()}", "false"
            off_curve = point[:-1] + bytes([point[-1] ^ 1])
            yield f"ecdsa_verify {name} {off_curve.hex()} {h(message)} {randomized.hex()}", "error invalid_key"
            compressed = key.public_key().public_bytes(serialization.Encoding.X962,
                                                       serialization.PublicFormat.CompressedPoint)
            yield f"ecdsa_verify {name} {compressed.hex()} {h(message)} {randomized.hex()}", "error invalid_key"
            yield f"private_der {pkcs8_der(key).hex()}", f"{pkcs8_der(key).hex()} {spki_der(key.public_key()).hex()}"
            traditional = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                            serialization.NoEncryption())
            expected = f"{pkcs8_der(key).hex()} {spki_der(key.public_key()).hex()} {pkcs8_pem(key).hex()}"
            yield f"private_pem {traditional.hex()}", expected
            yield f"private_pem {pkcs8_pem(key).hex()}", expected
            yield f"public_der {spki_der(key.public_key()).hex()}", spki_der(key.public_key()).hex()
            yield (f"public_pem {spki_pem(key.public_key()).hex()}",
                   f"{spki_der(key.public_key()).hex()} {spki_pem(key.public_key()).hex()}")
            yield f"verify_der {spki_der(key.public_key()).hex()} - {h(message)} {randomized.hex()}", "true"
        message = os.urandom(40)
        sizes = size + (2 * size + 1) + 2 * size
        yield f"ecdsa_generate {name} {message.hex()}", checks_generated("ecdsa", name, message, f"true true {sizes}")
    # openssl ecparam -genkey writes an EC PARAMETERS block before the key.
    key = ec.generate_private_key(ec.SECP256R1())
    traditional = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                    serialization.NoEncryption())
    parameters = b"-----BEGIN EC PARAMETERS-----\nBggqhkjOPQMBBw==\n-----END EC PARAMETERS-----\n"
    yield (f"private_pem {(parameters + traditional).hex()}",
           f"{pkcs8_der(key).hex()} {spki_der(key.public_key()).hex()} {pkcs8_pem(key).hex()}")
    encrypted = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                  serialization.BestAvailableEncryption(b"secret"))
    yield f"private_pem {encrypted.hex()}", "error unsupported"
    legacy = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                               serialization.BestAvailableEncryption(b"secret"))
    yield f"private_pem {legacy.hex()}", "error unsupported"
    yield f"private_pem {b'no key here'.hex()}", "error invalid_key"
    yield f"private_der {pkcs8_der(key)[:-1].hex()}", "error invalid_key"
    yield "private_der 3000", "error invalid_key"
    compressed = (bytes.fromhex("3039301306072a8648ce3d020106082a8648ce3d030107032200") +
                  key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.CompressedPoint))
    yield f"public_der {compressed.hex()}", "error unsupported"
    secp256k1 = ec.generate_private_key(ec.SECP256K1())
    yield f"private_der {pkcs8_der(secp256k1).hex()}", "error unsupported"
    yield f"public_der {spki_der(secp256k1.public_key()).hex()}", "error unsupported"
    dsa_key = dsa.generate_private_key(2048)
    yield f"private_der {pkcs8_der(dsa_key).hex()}", "error unsupported"
    # RSA: PKCS#1 v1.5 signatures are deterministic, PSS signatures are checked by verification.
    for bits, exponent in ((2048, 65537), (3072, 65537), (2048, 3)):
        key = rsa.generate_private_key(public_exponent=exponent, key_size=bits)
        public = key.public_key()
        numbers = public.public_numbers()
        modulus = numbers.n.to_bytes(bits // 8, "big")
        exponent_bytes = numbers.e.to_bytes((numbers.e.bit_length() + 7) // 8, "big")
        yield (f"rsa_components 00{modulus.hex()} 0000{exponent_bytes.hex()}",
               f"{modulus.hex()} {exponent_bytes.hex()} {bits}")
        yield f"private_der {pkcs8_der(key).hex()}", f"{pkcs8_der(key).hex()} {spki_der(public).hex()}"
        traditional = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                        serialization.NoEncryption())
        yield (f"private_pem {traditional.hex()}",
               f"{pkcs8_der(key).hex()} {spki_der(public).hex()} {pkcs8_pem(key).hex()}")
        pkcs1_public = public.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.PKCS1)
        yield f"public_pem {pkcs1_public.hex()}", f"{spki_der(public).hex()} {spki_pem(public).hex()}"
        yield f"public_der {spki_der(public).hex()}", spki_der(public).hex()
        for scheme in SCHEMES:
            message = os.urandom(rng.randrange(0, 100))
            if SCHEMES[scheme][0]:
                yield f"sign_der {pkcs8_der(key).hex()} {scheme} {h(message)}", checks_rsa_signature(public, scheme, message)
            else:
                yield f"sign_der {pkcs8_der(key).hex()} {scheme} {h(message)}", rsa_sign(key, scheme, message).hex()
            signature = rsa_sign(key, scheme, message)
            yield f"verify_der {spki_der(public).hex()} {scheme} {h(message)} {signature.hex()}", "true"
            other = "pss_sha256" if scheme == "pkcs1_sha256" else "pkcs1_sha256"
            yield f"verify_der {spki_der(public).hex()} {other} {h(message)} {signature.hex()}", "false"
            yield f"verify_der {spki_der(public).hex()} {scheme} {h(message)} {signature[:-1].hex()}", "false"
    small = rsa.generate_private_key(public_exponent=65537, key_size=1024)
    yield f"private_der {pkcs8_der(small).hex()}", "error invalid_key"
    yield f"public_der {spki_der(small.public_key()).hex()}", "error invalid_key"
    message = os.urandom(30)
    yield (f"rsa_generate 2048 pss_sha256 {message.hex()}",
           checks_generated("rsa", "pss_sha256", message, "true 4096"))
    yield "rsa_generate 1024 pkcs1_sha256 00", "error invalid_length"
    # Ed25519 and X25519 keys in PKCS#8 and SPKI.
    for key in (ed25519.Ed25519PrivateKey.generate(), x25519.X25519PrivateKey.generate()):
        yield f"private_der {pkcs8_der(key).hex()}", f"{pkcs8_der(key).hex()} {spki_der(key.public_key()).hex()}"
        yield (f"private_pem {pkcs8_pem(key).hex()}",
               f"{pkcs8_der(key).hex()} {spki_der(key.public_key()).hex()} {pkcs8_pem(key).hex()}")
        yield (f"public_pem {spki_pem(key.public_key()).hex()}",
               f"{spki_der(key.public_key()).hex()} {spki_pem(key.public_key()).hex()}")
    signer = ed25519.Ed25519PrivateKey.generate()
    message = os.urandom(20)
    yield f"sign_der {pkcs8_der(signer).hex()} - {message.hex()}", signer.sign(message).hex()
    yield f"verify_der {spki_der(signer.public_key()).hex()} - {message.hex()} {signer.sign(message).hex()}", "true"
    # AES-CBC with PKCS#7 against cryptography.
    for _ in range(16):
        key = os.urandom(rng.choice((16, 24, 32)))
        iv = os.urandom(16)
        plaintext = os.urandom(rng.randrange(0, 70))
        ciphertext = cbc_encrypt(key, iv, plaintext)
        yield f"cbc_encrypt {key.hex()} {iv.hex()} {h(plaintext)}", ciphertext.hex()
        yield f"cbc_decrypt {key.hex()} {iv.hex()} {ciphertext.hex()}", h(plaintext)
    key, iv = os.urandom(16), os.urandom(16)
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    bad = encryptor.update(bytes(15) + b"\x00") + encryptor.finalize()
    yield f"cbc_decrypt {key.hex()} {iv.hex()} {bad.hex()}", "error invalid_padding"
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    bad = encryptor.update(bytes(14) + b"\x03\x02") + encryptor.finalize()
    yield f"cbc_decrypt {key.hex()} {iv.hex()} {bad.hex()}", "error invalid_padding"
    yield f"cbc_decrypt {key.hex()} {iv.hex()} -", "error invalid_length"
    yield f"cbc_decrypt {key.hex()} {iv.hex()} {bad[:15].hex()}", "error invalid_length"
    yield f"cbc_encrypt {key[:15].hex()} {iv.hex()} 00", "error invalid_length"
    yield f"cbc_encrypt {key.hex()} {iv[:15].hex()} 00", "error invalid_length"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    rng = random.Random(28)
    pairs = list(cases(rng)) + list(public_key_cases(rng))
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
            shown = "a value its check accepts" if callable(expected) else expected[:100]
            print(f"FAIL {command[:60]}...\n  expected {shown}\n  actual   {actual[:100]}")
    if len(outputs) != len(pairs):
        failures += 1
        print(f"FAIL {len(outputs)} outputs for {len(pairs)} commands")
    print(f"crypto vectors: {len(pairs) - failures}/{len(pairs)} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
