# Binary packets and checksums

A small tool for inspecting binary representations. Run `hash TEXT` for all five
one-shot hash algorithms, `packet TEXT SEQUENCE` to encode and decode a telemetry
packet, `label TEXT` to create a padded device label, or `compare TEXT PATTERN` to
inspect byte ordering, prefix/suffix matches and offsets.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_binary --output-on-failure
build-debug/tests/codegen_example_binary hash abc
build-debug/tests/codegen_example_binary packet hello 42
build-debug/tests/codegen_example_binary label sensor
build-debug/tests/codegen_example_binary compare abc bc
```

`packet` prints its decoded fields followed by the complete hexadecimal wire representation.
Its format is one flag byte, a little-endian u16 version, u32 byte length, u64 sequence,
and UTF-8 payload. Flags demonstrate reading individual bits and aligning to a byte.
`label` produces exactly 16 bytes: `+`, up to 15 input bytes, then spaces. Labels are
binary fields; clipping does not promise a UTF-8 boundary.

[codec.r](src/codec.r) contains the algorithms; [main.r](src/main.r) handles arguments,
checked errors and async output. No hash call consumes or copies its source.
The Python command tests independently check hashes, packet layout, padding and search results.
CRC32 is printed in decimal; the four digest algorithms are printed in hexadecimal.
