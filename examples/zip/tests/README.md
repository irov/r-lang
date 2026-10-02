# Example tests

`archive_test.r` tests ZIP32 records constructed directly in R:

- an empty archive and EOCD;
- a local file header and central-directory entry for stored data;
- little-endian fields, the UTF-8 flag, offsets, sizes, and CRC32;
- path validation and collision detection.

`test_main.r` combines the tests into a separate hosted entry point,
`example.zip.tests.main`.

The `r_frontend_zip_unzip_roundtrip` integration CTest compiles the actual
`example.zip.main::main` and `example.unzip.main::main` programs. For five fixed seeds,
it generates nested and empty directories and files with binary, empty, repetitive,
and UTF-8 contents. It creates the archive twice to verify determinism, reads it
independently using Python's `zipfile`, then runs R `unzip` and compares the entire
tree byte-for-byte. Separate safety cases cover an empty archive, no-replace behavior,
unsafe names, and path collisions. On failure, the harness prints the seed and a replay command.
