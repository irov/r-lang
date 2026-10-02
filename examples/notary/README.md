# Signed and sealed messages with COSE

Sign, verify, seal and open short messages as COSE messages (RFC 9052) with keys derived from a
passphrase, and show any CBOR item in diagnostic notation. `std.cose` builds the messages over
`std.cbor` and `std.crypto`, whose native provider calls libsodium (Library R-SLIB-CBOR-0001..0006,
R-SLIB-CRYPTO-0001..0008, R-SLIB-COSE-0001..0006).

```sh
ctest --test-dir build/debug -R 'example_notary' --output-on-failure
build/debug/tests/codegen_example_notary demo
build/debug/tests/codegen_example_notary sign 'correct horse battery staple' 'Pay 10 coins to Bob'
build/debug/tests/codegen_example_notary verify PUBLIC_HEX MESSAGE_HEX
build/debug/tests/codegen_example_notary show a26161016162820203
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

The behaviour test checks the keys, signatures, messages and digests against the Python
`cryptography` package and an independent CBOR codec: a message signed here verifies there, and a
sealed message opens there.
