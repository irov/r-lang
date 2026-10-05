# Signed and sealed messages with COSE

Sign, verify, seal and open short messages as COSE messages (RFC 9052) with keys derived from a
passphrase, and show any CBOR item in diagnostic notation. `std.cose` builds the messages over
`std.cbor` and `std.crypto`, whose native provider calls libsodium and Mbed TLS (Library
R-SLIB-CBOR-0001..0006, R-SLIB-CRYPTO-0001..0012, R-SLIB-COSE-0001..0006).

```sh
ctest --test-dir build/debug -R 'example_notary' --output-on-failure
build/debug/tests/codegen_example_notary demo
build/debug/tests/codegen_example_notary sign 'correct horse battery staple' 'Pay 10 coins to Bob'
build/debug/tests/codegen_example_notary verify PUBLIC_HEX MESSAGE_HEX
build/debug/tests/codegen_example_notary show a26161016162820203
build/debug/tests/codegen_example_notary keys 'correct horse battery staple'
build/debug/tests/codegen_example_notary cbc 'correct horse battery staple' 'meet at noon'
```

`demo` signs a message, verifies it, shows it, and shows that a changed message and another key
are refused, then seals and opens a text:

```text
public key 8184ab07c4e83d500cb258cc06af2b06dbfb6d98fbfa6829e4a15164766cbbd4
signed 101 bytes
18([h'a10127', {4: h'6e6f74617279'}, h'50617920313020636f696e7320746f20426f62', h'a684d5a8...c90c'])
key id: notary
raw signature valid: true
verified: Pay 10 coins to Bob
changed message refused: verification_failed
other key refused: verification_failed
opened: meet at noon
```

[keys.r](src/keys.r) stretches the passphrase with Argon2id into the 32-byte seed of an Ed25519
key and derives the sealing key from it with HKDF. [main.r](src/main.r) makes a COSE_Sign1 message
whose covered bucket names EdDSA and whose other bucket carries a key identifier:

```r
std.cose::headers fields = std.cose::headers::create();
fields.protect(std.cose::ALGORITHM, std.cbor::value::integer(std.cose::EDDSA));
fields.expose(std.cose::KEY_ID, std.cbor::value::of_bytes("notary"));
bytes sealed = std.cose::sign1(move fields, message, &key, "");
```

`verify` checks such a message against a public key and prints its payload, or exits with 65 and
the reason. Ed25519 signatures are deterministic, so the same passphrase and message always give the
same bytes. `seal` and `open` use COSE_Encrypt0 with ChaCha20-Poly1305 and a fresh random IV for
every message.

[tools.r](src/tools.r) has the other commands. `exchange NOTE` lets two parties agree on a secret
with X25519, derive a key with HKDF-SHA-512 and pass the note sealed with XChaCha20-Poly1305;
`password SECRET` makes an Argon2id password hash and checks it; `digest TEXT` prints BLAKE2b-256,
SHA-384 and HMAC-SHA-384 of the text, the first two also computed from two pieces; `tag KEY MESSAGE`
makes COSE_Mac0 messages with each HMAC algorithm; and `document` writes a CBOR map item by item
with `std.cbor::encoder`, in the key order that makes it deterministic, reads it back with
`decode_deterministic` and shows it.

[pki.r](src/pki.r) works with the public-key formats that other tools exchange. `keys PASSPHRASE`
derives the private scalar of an ES256 key (ECDSA on P-256 with SHA-256) from the passphrase,
prints its public key as SPKI PEM and its signature of a fixed message, reads both keys back from
their PEM text, and signs with a fresh 2048-bit RSA key under PKCS#1 v1.5 and PSS:

```text
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEradYDFoHjNKntkoy+wpQ9UO9NCoD
PyI9NkNsxPcJZs2czaa/s22sgVWBADsUAhAc1Odv3/bVV60vIsvJIcKdBg==
-----END PUBLIC KEY-----
ES256 signature, 64 bytes: 26670fbd529570395c9704760f0f28ae8a971076f6131eb07b9c8f18ca3410e86578e6ea43927c2ef62bfef6712bceef95d2db460452095d847d79b6442ec03f
read back from PEM: valid true, other message false, same signature true
RSA 2048 bits: PKCS#1 v1.5 true, PSS true, PSS check of the PKCS#1 signature false
```

ECDSA signatures here are the deterministic ones of RFC 6979, so the signature is the same on
every run. The private key goes through `std.crypto::private_key`, whose PEM text stays in a
secret buffer:

```r
std.crypto::private_key wrapped = std.crypto::private_key::ecdsa(move key);
std.secret::buffer private_pem = wrapped.to_pem();
std.crypto::private_key read = std.crypto::private_key::from_pem(std.secret::as_slice(&private_pem));
```

`cbc PASSPHRASE TEXT` encrypts the text with AES-128-CBC and PKCS#7 padding under a key and an IV
of the passphrase, decrypts it, and shows that a changed last block is refused. CBC does not
authenticate the data; it is here for protocols that require it:

```text
ciphertext fae793a0cdafd28dcbf93e8ee16fddc6
plaintext 6d656574206174206e6f6f6e
changed last block: invalid_padding
```

The behaviour test checks the keys, signatures, messages and digests against the Python
`cryptography` package and an independent CBOR codec: a message signed here verifies there, and a
sealed message opens there.
