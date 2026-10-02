# Tokens

Identifiers, signatures and random draws without hand-written crypto: `std.uuid`
(Library R-SLIB-UUID-0001..0003) makes and parses the identifiers of RFC 9562, `std.hash`
(R-SLIB-BYTES-0010..0011) digests data given in pieces and signs messages with HMAC, and
`std.random` (R-SLIB-RANDOM-0001..0002) draws bytes of the operating-system generator and
reproducible numbers from a seed.

```sh
ctest --test-dir build-debug -R 'example_tokens' --output-on-failure
build-debug/tests/codegen_example_tokens id v7 3
build-debug/tests/codegen_example_tokens sign secret 'hello world'
printf 'some input' | build-debug/tests/codegen_example_tokens digest
build-debug/tests/codegen_example_tokens dice 42 10
```

`tokens id v4|v7 COUNT` prints fresh identifiers; version 7 identifiers begin with the time in
milliseconds, so they sort by the moment they were made. `tokens id parse TEXT` reads the
36-character form in either case and prints it in lowercase with its version; `tokens id at
MILLISECONDS HEX20` shows how the version 7 and version 4 layouts place a time and random bytes:

```text
$ tokens id at 0x017f22e279b0 0cc318c4dc0c0c07398f
v7 017f22e2-79b0-7cc3-98c4-dc0c0c07398f
v4 0cc318c4-dc0c-4c07-b98f-0cc318c4dc0c
```

`tokens sign KEY MESSAGE` prints HMAC-SHA-256 in hexadecimal and base64url and HMAC-SHA-512;
`tokens verify KEY MESSAGE HEX` compares a received code with `std.secret::constant_time_equal`,
which takes the same time wherever the codes differ:

```r
bytes given = std.encoding::decode_hex(code);
std.hash::sha256_digest expected = std.hash::hmac_sha256(key, message);
return std.secret::constant_time_equal(given.as_slice(), expected.bytes);
```

`tokens digest` hashes standard input in reads of up to 4096 bytes: one `sha256_state` and one
`sha512_state` take every piece, so the input never has to fit in memory.

`tokens dice SEED COUNT` and `tokens shuffle SEED ITEM...` draw from
`std.random::generator::seeded(seed)`, xoshiro256** seeded by SplitMix64: the same seed gives
the same rolls and the same order on every run and every implementation, which makes tests and
simulations repeatable. `tokens noise COUNT` and `tokens pick LOW HIGH` draw from the
operating-system generator instead, which is what keys and nonces need.
