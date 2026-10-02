# Compress and decompress DEFLATE, zlib and gzip streams

A command-line filter over `std.deflate`. It reads stdin, runs the resumable coders in
chunks of a chosen size against a caller-owned output buffer, and writes the result to
stdout. Run without arguments for the complete command syntax.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_deflate --output-on-failure
printf 'hello hello hello' | build-debug/tests/codegen_example_deflate deflate rfc1950 6 | xxd
printf 'hello hello hello' | build-debug/tests/codegen_example_deflate deflate_sync rfc1952 9 4096 5 | gunzip
gzip -c README.md | build-debug/tests/codegen_example_deflate inflate rfc1952
gzip -c README.md | build-debug/tests/codegen_example_deflate stats rfc1952 1048576 32768 7
printf 'abc' | build-debug/tests/codegen_example_deflate pack rfc1951 1 | build-debug/tests/codegen_example_deflate unpack rfc1951
```

`deflate FORMAT LEVEL [WINDOW] [CHUNK]` compresses with `std.deflate::deflater`; the level is
0 through 9, the window a power of two from 256 to 32768, and the chunk the size of every
input piece and of the output buffer. `deflate_sync` additionally requests `flush::sync`
after every chunk, so each piece ends on a byte boundary. `inflate FORMAT [LIMIT] [WINDOW]
[CHUNK]` decompresses with `std.deflate::inflater`; the limit bounds the produced bytes and
turns a decompression bomb into the checked error `output_limit`. `pack` and `unpack` are the
one-shot `std.deflate::deflate` and `std.deflate::inflate`. `stats` decodes the stream,
resets the coder, decodes it again and prints the consumed and produced totals with the number
of steps.

Formats are named after their RFCs: `rfc1951` is raw DEFLATE, `rfc1950` the zlib wrapper with
an Adler-32 trailer, `rfc1952` the gzip wrapper with CRC-32 and size. A malformed or truncated
stream, a checksum mismatch, input after the end of the stream, an invalid level or window and
an exceeded limit are reported on stderr with exit status 65; usage errors exit with 64.

`tests/run_deflate_examples.py` is a differential test: Python's zlib decompresses what this
program compresses at every level, format, window and chunk size, and this program
decompresses what zlib and gzip produce, including gzip members with optional header fields.
