# `zip`: self-contained ZIP creation in R

This is a multi-module ZIP writer example for R 0.1. ZIP headers and the central
directory are constructed directly in R, while CRC32 is computed by the standard
`std.hash::crc32` operation. The example does not call zlib, libarchive, or another
archiving library through `extern "C"`.

The first version creates portable ZIP32 archives using method `0` (`stored`). It
packages files without compression, producing ordinary ZIP archives readable by
standard ZIP tools. This restriction keeps the example small and lets tests verify
its format implementation independently of the DEFLATE decoder already implemented
in the adjacent `unzip` example.

## Implemented features

- local file headers, a central directory, and EOCD for ZIP32;
- regular files and explicit directory entries;
- the UTF-8 flag and strict validation of UTF-8 names;
- CRC32 of each file's contents;
- deterministic output: identical bytes and argument order produce an identical
  archive;
- rejection of absolute paths, `.`/`..`, empty components, backslashes, colons, NUL,
  and control bytes;
- detection of duplicate, ASCII-case-fold, and file-as-parent conflicts before
  reading files;
- async `std.fs::read_file` for input and atomic no-replace publication relative to
  the output directory capability;
- owned buffers in all async operations.

Format and limit checks use `throw (condition) ZipError { ... };`, shorthand for
`if (condition) { throw ZipError { ... }; }`. When the condition is false, the error
payload is not created; when true, ordinary checked-error propagation applies.

Limits are defined in `src/model.r`:

| Limit | Value |
|---|---:|
| Single input file size | 16 MiB |
| Final archive size | 32 MiB |
| Number of entries | 1024 |
| Entry name | 4096 bytes |

## CLI

The current compiler milestone does not yet lower directory iteration to C17, so
the reference CLI accepts an explicit list of relative entries:

```text
zip <existing-output-directory> <archive-name> <entry>...
```

Run the command from the input root. A trailing `/` denotes a directory:

```sh
cd input-tree
zip ../archives sample.zip empty/ images/ images/icon.bin readme.txt
```

Argument order determines the order of archive entries. The output directory must
exist, and `archive-name` must be a safe relative name. An existing archive is never
overwritten. Invoking the command without entries creates a valid empty ZIP.

For each regular file, the `read_file` task is started and immediately resolved by
`await std.fs::read_file(...)` before proceeding to the next entry. The example uses
native async I/O without promising parallel reads of multiple files: sequential
execution preserves deterministic output and makes each task's ownership statically clear.

Once the frontend supports C17 lowering for `std.fs::iterate`, the same writer can
be wrapped in a recursive directory walker without changing the archive format.
For now, tree traversal is deliberately handled by the caller and randomized test harness.

## Structure

```text
src/
  main.r       async CLI, validation, and sequential entry packaging
  archive.r    local/central/EOCD ZIP32 records
  name.r       UTF-8, traversal, and collision checks
  bytes.r      thin adapters over `bytes` and `std.bytes::append_*`
  host.r       native-async std.fs/std.io boundary
  model.r      limits
  error.r      typed errors
tests/
  archive_test.r  empty/stored archive records and CRC vectors
  test_main.r     hosted pure-test entry point
  README.md       pure-test and actual zip-to-unzip integration contracts
```

## Building and testing

Run the frontend and strict-C17 backend as follows:

```sh
build/r-front --emit=c17 \
  --module-map examples/zip/modules.map \
  --entry example.zip.main::main \
  --profile hosted-native-async \
  --target-manifest targets/arm64-apple-darwin.hosted-native-async.json \
  > zip.c
```

CTest builds and tests both programs:

```sh
ctest --test-dir build -R \
  'r_frontend_codegen_zip_(pure|full)|r_frontend_zip_unzip_roundtrip' \
  --output-on-failure
```

For five fixed seeds, `r_frontend_zip_unzip_roundtrip`:

1. builds a random tree with nested and empty directories;
2. adds files with binary, empty, repetitive, and UTF-8 contents;
3. runs the generated R `zip` twice and compares the archives byte-for-byte;
4. reads the archive independently with Python's `zipfile` and verifies CRC;
5. runs the generated R `unzip`;
6. compares directory names and the bytes of each extracted file;
7. separately checks no-replace behavior, unsafe names, and path collisions.

On failure, the harness prints the seed and a ready-to-use replay command. After
the test, generated artifacts are available at `build/tests/codegen_zip_full.c` and
`build/tests/codegen_zip_full`.

An immediate start followed by an await needs no intermediate task variable.
Examples of individual calls from `src/main.r`:

```r
await write_output(move output_path, move archive_path, move output);
bytes contents = await read_entry(move input_path);
```

When work or explicit task management is needed between start and await, retain
a named task and use `await move task_name`. Both forms handle start and completion
errors identically.

Diagnostics in `src/host.r` are built using `f"zip: {error.message}\n"`.
The resulting `std.string::string` transfers its buffer through `std.string::into_bytes`
for async writing; allocation remains a checked operation.
