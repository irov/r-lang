# Example tests

Independent raw-DEFLATE vectors are split across test modules:

- `stored_block_test.r` — a final stored block for `hello`;
- `fixed_huffman_test.r` — a fixed-Huffman stream for `hello hello hello!`;
- `dynamic_huffman_test.r` — a dynamic-Huffman stream with a back-reference and CRC32;
- `deflate_validation_test.r` — a negative vector with a byte after the final block.

`test_main.r` combines them into a separate hosted entry point,
`example.unzip.tests.main`.

The `r_frontend_zip_unzip_roundtrip` integration CTest compiles the actual
`example.zip.main::main` and `example.unzip.main::main` programs, then uses five fixed
seeds to create trees with nested and empty directories and files with binary, empty,
repetitive, and UTF-8 contents. It packages the same input twice, verifies archive
determinism, reads the ZIP using Python's `zipfile`, runs R `unzip`, and compares the
extracted tree byte-for-byte. A separate safety group covers an empty archive,
no-replace behavior, unsafe names, and path collisions.

Future extensions to the integration suite should add dedicated ZIP32 fixtures:

- stored and deflated files, an empty file, and a directory;
- data descriptors with and without a signature;
- CRC mismatch and truncated central/local headers;
- `../escape`, absolute paths, backslashes, NUL, and invalid UTF-8;
- duplicate/case-fold collisions and file-as-parent conflicts;
- symlinks/special Unix entries, encryption, ZIP64, and multi-disk archives;
- overlapping local records, oversized output, and compression-ratio bombs;
- verification of the `hosted-native-async` backend's no-replace and symlink-race guarantees;
- injected task-start failure, cancellation acknowledgement, and exactly-once return
  of an owned I/O buffer.
