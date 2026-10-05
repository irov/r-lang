#!/usr/bin/env python3
"""Commands of examples/notary: COSE messages of RFC 9052 (std.cose) signed with an Ed25519 key
and sealed with ChaCha20-Poly1305 under keys derived from a passphrase with Argon2id and HKDF
(std.crypto), shown in CBOR diagnostic notation (std.cbor); an ES256 key of the passphrase in
PKCS#8 and SPKI PEM, a fresh RSA key and AES-CBC (M36). The keys, signatures and messages are
checked against the Python `cryptography` package and an independent CBOR codec."""
import argparse
from pathlib import Path
import subprocess
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import padding as block_padding
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.argon2 import Argon2id
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cbor_vectors import Map, Tag, decode, encode  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
usage = ('notary public_key PASSPHRASE | sign PASSPHRASE MESSAGE | verify PUBLIC MESSAGE_HEX\n'
         'notary show MESSAGE_HEX | seal PASSPHRASE TEXT | open PASSPHRASE MESSAGE_HEX\n'
         'notary exchange NOTE | password SECRET | digest TEXT | tag KEY MESSAGE | document | demo\n'
         'notary keys PASSPHRASE | cbc PASSPHRASE TEXT\n')
checks = 0


def run(values):
    result = subprocess.run([args.executable, *values], capture_output=True, timeout=120)
    return result.returncode, result.stdout.decode(), result.stderr.decode()


def expect(values, status, stdout, stderr=''):
    global checks
    outcome = run(values)
    assert outcome == (status, stdout, stderr), (values, outcome)
    checks += 1


def seed(passphrase: str) -> bytes:
    return Argon2id(salt=b'example.notary.1', length=32, iterations=2, lanes=1,
                    memory_cost=8192).derive(passphrase.encode())


def signer(passphrase: str) -> ed25519.Ed25519PrivateKey:
    return ed25519.Ed25519PrivateKey.from_private_bytes(seed(passphrase))


def public_hex(passphrase: str) -> str:
    return signer(passphrase).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()


def sealing_key(passphrase: str) -> bytes:
    return HKDF(algorithm=hashes.SHA256(), length=32, salt=b'example.notary', info=b'seal').derive(seed(passphrase))


def expected_sign1(passphrase: str, message: str) -> bytes:
    protected = encode(Map([(1, -8)]))
    unprotected = Map([(4, b'notary')])
    covered = encode(['Signature1', protected, b'', message.encode()])
    signature = signer(passphrase).sign(covered)
    return encode(Tag(18, [protected, unprotected, message.encode(), signature]))


expect([], 0, '', usage)
expect(['bogus'], 64, '', usage)
expect(['sign', 'only-one'], 64, '', usage)

passphrase = 'correct horse battery staple'
expect(['public_key', passphrase], 0, public_hex(passphrase) + '\n')

# Ed25519 is deterministic: the message equals the one built here, and its signature verifies
# with the public key of `cryptography`.
for text in ('Pay 10 coins to Bob', 'ünïcode ✓', 'x'):
    code, out, err = run(['sign', passphrase, text])
    assert code == 0 and err == '', (code, out, err)
    message = bytes.fromhex(out.strip())
    assert message == expected_sign1(passphrase, text), (text, out)
    item = decode(message)
    assert isinstance(item, Tag) and item.number == 18 and item.content[2] == text.encode()
    covered = encode(['Signature1', item.content[0], b'', item.content[2]])
    signer(passphrase).public_key().verify(item.content[3], covered)
    expect(['verify', public_hex(passphrase), out.strip()], 0, f'valid: {text}\n')
    checks += 1

message = expected_sign1(passphrase, 'Pay 10 coins to Bob')
changed = bytearray(message)
changed[-67] ^= 1
expect(['verify', public_hex(passphrase), changed.hex()], 65, 'invalid: verification_failed\n')
expect(['verify', public_hex('another passphrase'), message.hex()], 65, 'invalid: verification_failed\n')
expect(['verify', public_hex(passphrase), message.hex()[:40]], 65, 'invalid: malformed\n')
expect(['verify', 'zz', message.hex()], 64, '', usage)

# Diagnostic notation of the signed message and of other CBOR.
signature_hex = message[-64:].hex()
expect(['show', message.hex()], 0,
       "18([h'a10127', {4: h'6e6f74617279'}, h'50617920313020636f696e7320746f20426f62', "
       f"h'{signature_hex}'])\n")
expect(['show', 'a26161016162820203'], 0, '{"a": 1, "b": [2, 3]}\n')
expect(['show', 'ff'], 65, '', 'not CBOR: malformed\n')

# Sealed messages open with the key of `cryptography`, and only with the right passphrase.
code, out, err = run(['seal', passphrase, 'meet at noon'])
assert code == 0 and err == '', (code, out, err)
sealed = bytes.fromhex(out.strip())
item = decode(sealed)
assert isinstance(item, Tag) and item.number == 16, item
protected, unprotected, ciphertext = item.content
assert protected == encode(Map([(1, 24)])), protected
iv = dict(unprotected)[5]
aad = encode(['Encrypt0', protected, b''])
assert ChaCha20Poly1305(sealing_key(passphrase)).decrypt(iv, ciphertext, aad) == b'meet at noon'
checks += 1
expect(['open', passphrase, out.strip()], 0, 'meet at noon\n')
expect(['open', 'wrong passphrase', out.strip()], 65, 'invalid: verification_failed\n')
code, second, _ = run(['seal', passphrase, 'meet at noon'])
assert code == 0 and second != out, 'a fresh IV for every message'
checks += 1

code, out, err = run(['demo'])
assert code == 0 and err == '', (code, out, err)
lines = out.splitlines()
assert lines[0] == f'public key {public_hex(passphrase)}', lines[0]
assert lines[1] == f'signed {len(message)} bytes', lines[1]
assert lines[2].startswith("18([h'a10127', {4: h'6e6f74617279'}"), lines[2]
assert lines[3:5] == ['key id: notary', 'raw signature valid: true'], lines
assert lines[5:] == ['verified: Pay 10 coins to Bob', 'changed message refused: verification_failed',
                     'other key refused: verification_failed', 'opened: meet at noon'], lines
checks += 1

# The other commands: X25519 agreement, Argon2id hashes, digests, MAC tags and a CBOR document.
expect(['exchange', 'hello'], 0, 'agreed: true\nsealed with 16 bytes of tag (16)\nbob read 68656c6c6f\n')
expect(['password', 'pw'], 0, 'argon2id hash: true\nsame password: true, other password: false\n')
import hashlib
import hmac as hmac_module
for text in ('abc', 'notary digests'):
    blake = hashlib.blake2b(text.encode(), digest_size=32).hexdigest()
    sha = hashlib.sha384(text.encode()).hexdigest()
    mac = hmac_module.new(b'notary', text.encode(), hashlib.sha384).hexdigest()
    expect(['digest', text], 0, f'blake2b-256 {blake} (pieces agree: true)\n'
           f'sha-384 {sha} (pieces agree: true)\nhmac-sha-384 {mac}\n')
expect(['tag', 'key', 'msg'], 0, 'mac0 algorithm 4: 8-byte tag\nmac0 algorithm 5: 32-byte tag\n'
       'mac0 algorithm 6: 48-byte tag\nmac0 algorithm 7: 64-byte tag\n')
code, out, err = run(['document'])
assert code == 0 and err == '', (code, out, err)
assert out == ('40 bytes {"note": 24(h\'6869\'), "flags": [true, null, undefined, simple(32), 0.5, -10], '
               '"issued": 1(1700000000)}\nencode agrees: true, 41 bytes with a trailing -1\n'
               'entries {"issued": 1, "note": 2} encode as '
               + encode(Map([('issued', 1), ('note', 2)])).hex() + '\n'), out
checks += 1


def derived(passphrase: str, label: bytes, length: int) -> bytes:
    return HKDF(algorithm=hashes.SHA256(), length=length, salt=b'example.notary', info=label).derive(seed(passphrase))


# M36: the ES256 key of the passphrase, its PEM and its deterministic signature (RFC 6979) agree
# with `cryptography`; the RSA line holds no key material.
es256 = ec.derive_private_key(int.from_bytes(derived(passphrase, b'es256', 32), 'big'), ec.SECP256R1())
public_pem = es256.public_key().public_bytes(serialization.Encoding.PEM,
                                             serialization.PublicFormat.SubjectPublicKeyInfo).decode()
r, s = decode_dss_signature(es256.sign(b'Pay 10 coins to Bob', ec.ECDSA(hashes.SHA256(), deterministic_signing=True)))
signature = (r.to_bytes(32, 'big') + s.to_bytes(32, 'big')).hex()
expect(['keys', passphrase], 0,
       public_pem + f'ES256 signature, 64 bytes: {signature}\n'
       'read back from PEM: valid true, other message false, same signature true\n'
       'RSA 2048 bits: PKCS#1 v1.5 true, PSS true, PSS check of the PKCS#1 signature false\n')
expect(['keys'], 64, '', usage)

for text in ('meet at noon', 'sixteen bytes!!!', ''):
    key, iv = derived(passphrase, b'cbc-key', 16), derived(passphrase, b'cbc-iv', 16)
    padder = block_padding.PKCS7(128).padder()
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    sealed = encryptor.update(padder.update(text.encode()) + padder.finalize()) + encryptor.finalize()
    changed = sealed[:-1] + bytes([sealed[-1] ^ 1])
    decryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    block = decryptor.update(changed) + decryptor.finalize()
    pad = block[-1]
    if 1 <= pad <= 16 and block[-pad:] == bytes([pad]) * pad:
        outcome = f'{len(block) - pad} bytes with a padding that happens to check'
    else:
        outcome = 'invalid_padding'
    expect(['cbc', passphrase, text], 0,
           f'ciphertext {sealed.hex()}\nplaintext {text.encode().hex()}\nchanged last block: {outcome}\n')
print(f'notary examples: {checks} checks passed')
