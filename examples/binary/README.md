# Binary packets and checksums

A small tool for inspecting binary representations. Run `hash TEXT` for all five
one-shot hash algorithms, `packet TEXT SEQUENCE` to encode and decode a telemetry
packet, `label TEXT` to create a padded device label, `compare TEXT PATTERN` to
inspect byte ordering, prefix/suffix matches and offsets, `bits VALUE` for the bit counts,
byte swap and rotations of a 64-bit value, or `wide LEFT RIGHT` for 128-bit arithmetic on
two 64-bit limbs.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_binary --output-on-failure
build-debug/tests/codegen_example_binary hash abc
build-debug/tests/codegen_example_binary packet hello 42
build-debug/tests/codegen_example_binary label sensor
build-debug/tests/codegen_example_binary compare abc bc
build-debug/tests/codegen_example_binary bits 81985529216486895
build-debug/tests/codegen_example_binary wide 18446744073709551615 2
```

`packet` prints its decoded fields followed by the complete hexadecimal wire representation.
Its format is one flag byte, a little-endian u16 version, u32 byte length, u64 sequence,
and UTF-8 payload. Flags demonstrate reading individual bits and aligning to a byte. The four
header fields form the struct `Header`, written and read by the generic `put_fields` and
`take_fields`: their `fields(Wire)` constraint asks every field to implement `Wire`, and a
translation-time loop `for (constexpr usize index in 0usize..core::field_count::<T>())` repeats
its block for each field, which `core::field` or `core::field_mut` borrows with its own type.
`label` produces exactly 16 bytes: `+`, up to 15 input bytes, then spaces. Labels are
binary fields; clipping does not promise a UTF-8 boundary.

`bits` prints the leading and trailing zero bits and the one bits of the value, then the value
with its bytes reversed and rotated left and right by one byte, in hexadecimal. `wide` prints the
high and low halves of the product, the sum and the difference with their carry and borrow, and
the product divided back by the second operand (`quotient none` for zero). They use the bit and
wide integer operations of `core` (`core::count_ones_u64`, `core::widening_mul_u64`,
`core::narrowing_div_u64` and others), which the C compiler turns into single instructions where
the target has them.

[codec.r](src/codec.r) contains the algorithms; [main.r](src/main.r) handles arguments,
checked errors and async output. No hash call consumes or copies its source.
The Python command tests independently check hashes, packet layout, padding and search results.
CRC32 is printed in decimal; the four digest algorithms are printed in hexadecimal.
