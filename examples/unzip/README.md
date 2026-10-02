# `unzip`: self-contained ZIP extraction in R

This is a deliberately complete multi-module example for R 0.1. ZIP and DEFLATE
are implemented in the example's source files. It uses no `extern "C"`, zlib,
libarchive, or other extraction library.

The only platform boundary is in `src/host.r`. It uses the normative `std.fs`,
`std.io`, and `std.async` modules from the R Standard Library Specification.
All potentially blocking operations run through the native async backend;
the ZIP parser and DEFLATE remain pure, synchronous computations over memory,
while CRC32 is computed by the standard pure operation `std.hash::crc32`.

## Implemented features

- ZIP32 with a regular EOCD and central directory;
- methods `0` (stored) and `8` (raw DEFLATE);
- stored, fixed-Huffman, and dynamic-Huffman DEFLATE blocks;
- canonical Huffman code construction and decoding;
- overlapping LZ77 back-references;
- data descriptors with and without a signature;
- CRC32 of each extracted file;
- strict validation of UTF-8 and safe relative paths;
- two sequential async extraction phases with shared immutable
  `arc ArchiveImage` and `arc Catalog`;
- a directory capability in `arc std.fs::directory` and owned `bytes` for each
  async write.

The example deliberately uses a restricted profile rather than silently accepting
every historical ZIP variation. It rejects ZIP64, multi-disk archives, encryption,
unknown methods and flags, symlinks, and special Unix files. Before producing any
output, it also rejects conflicting paths, file-as-parent conflicts, overlapping
local records, inconsistent headers, and inconsistent data descriptors.

Memory and zip-bomb protection limits are defined in `src/model.r`:

| Limit | Value |
|---|---:|
| Input ZIP size | 32 MiB |
| Single extracted file | 16 MiB |
| Total output | 64 MiB |
| Number of entries | 1024 |
| Path inside the archive | 4096 bytes |
| Compression ratio per entry | 200:1 |

The entire archive is read into owned `bytes` and then moved into `arc ArchiveImage`.
Each extraction phase creates its own `own Scratch*`; the next phase starts only
after the previous one has been awaited with `await`. Before an async write, the
result is copied from the scratch/archive view into separate owned `bytes`;
a borrowed slice never survives I/O submission.

## Structure

```text
src/
  main.r       async CLI, arc ownership, and sequential extraction phases
  zip.r        EOCD, central/local headers, and data descriptors
  deflate.r    DEFLATE blocks and LZ77
  huffman.r    canonical Huffman tables
  bitstream.r  ZIP error adapter over std.bits::lsb_reader
  path.r       UTF-8, traversal, and collision checks
  worker.r     pure decoding and async extraction of one catalog range
  host.r       boundary with native-async std.fs/std.io
  model.r      bounded storage and shared models
  bytes.r      safe reading of little-endian fields
  error.r      typed errors
tests/
  stored_block_test.r       stored DEFLATE and CRC32
  fixed_huffman_test.r      fixed-Huffman DEFLATE and CRC32
  dynamic_huffman_test.r    dynamic-Huffman DEFLATE and CRC32
  deflate_validation_test.r negative trailing-byte vector
  test_main.r               separate hosted test entry point
```

Module declarations are exported by default. The cross-module API therefore uses
no modifier, while local helpers and constants are explicitly marked `protected`.

## Reading the new syntax

- Aggregate allocation omits unnecessary parentheses:
  `new Scratch{}` and `new arc ArchiveImage { .bytes = move archive_bytes }`.
  Parentheses remain for expression initializers, such as `new rc i32(7)`.
- Checked errors are declared after the parameters: `T operation(...) throws E1, E2`.
  An ordinary `return value;` always completes the function successfully, and a
  `void` function may end at its closing `}`. An error is raised with `throw error;`,
  `throw move error;`, or an aggregate initializer. The explicit form
  `throw ZipError { .code = ..., ... };` is valid with any number of error types.
  The shorthand `throw { .code = ..., ... };` is valid only when exactly one error
  type is possible at that point; with multiple types, the type must be specified.
- Checks containing a single `throw` use `throw (condition) ZipError { ... };`.
  This is shorthand for `if (condition) { throw ZipError { ... }; }`: the payload
  is not evaluated when the condition is false. The result check in `src/deflate.r`
  demonstrates the shorthand form:

  ```r
  throw (written != expected_size) {
      .code = ZipErrorCode::InvalidDeflate,
      .offset = written,
      .message = "decoded size differs from central directory",
  };
  ```

- A call to a throwing operation automatically routes the exact error to the
  nearest `catch (ErrorType error)` or outward through a compatible `throws`.
  There is no separate propagation prefix operator. Such a call may appear only
  as a complete initializer, a complete assignment RHS, or a standalone `void` statement.
- `try { ... } catch (...) { ... } finally { ... }` is a statement. Typed catches
  do not catch panics. `throw;` rethrows the current value of the nearest catch
  binding, skipping sibling catches. `finally` runs exactly once on every exit,
  cannot replace the pending control transfer, and cannot contain `await`.
  This example does not keep conditionally active tasks in `finally`: each task
  is started and resolved along a single, linear ownership path.
- `value as void;` explicitly marks a Copy value as intentionally ignored. The
  operand is still evaluated; this neither suppresses errors nor destroys a Move object.
- Calling `async T operation(...) throws E` starts a task eagerly, creates
  `task<T throws E>`, and may separately throw an immediate `std.async::start_error`.
  `main` leaves that error to the implicit `main` error boundary, which reports it
  with status 116. After a successful start, the value or completion error is retrieved through
  `await`. The form `await operation(...)` combines the call and await;
  a task started earlier uses `await move operation_task`.
  In `main.r`, reading the archive, opening the output capability, and extracting
  the two halves run sequentially: each successfully created task is immediately
  awaited before the next starts. This makes must-resolve ownership statically unambiguous.
- Standard async I/O operations take call-bounded path/handle borrows and owned
  buffers. The runtime independently retains or copies the handle and path before
  successfully returning the task, while `bytes` is passed through `move`.
  Successful completion or a write error returns the same buffer in a closed
  outcome, after which the example destroys it.
- `constexpr str` is a distinct, safe Copy string type. Its immutable UTF-8 bytes
  are embedded in the program image and live for the entire execution; the
  descriptor itself can be selected at runtime, copied, passed, returned, and
  stored in a runtime structure such as `ZipError`. The descriptor has an ordinary
  object lifetime: ending its lifetime or overwriting it does not modify or destroy
  the program bytes. If the descriptor itself must not change after initialization,
  use `const constexpr str`; this is required for a module/static descriptor read
  directly from safe code without synchronization.
  An ordinary or dynamically created `str` cannot be converted back to
  `constexpr str`. Implicit string conversions do not chain.
  A direct `const u8[]` argument receives a call-bounded zero-copy view from a named
  `constexpr str`, `str`, `bytes`, `array<u8>`, or fixed `u8[N]`.
- `o<T>` is a tagged optional value: `o::some(T)` contains a value, while `o::none`
  denotes its absence without a separate error payload.
- `error ZipError { ... };` declares a type, but a type position uses just
  `ZipError`; forms such as `error ZipError error` are invalid in R.
- Module declarations are exported without a modifier; `protected` keeps a helper
  within its defining module. `static` does not control visibility and applies
  only to block objects with static storage duration.
- In `u8[11] compressed = { 0xcb, ... };`, both the array size and its elements
  receive their types from context. An unsuffixed integer literal also receives
  the known destination integer type during initialization, simple assignment,
  passing by value, and `return`. Arithmetic and comparisons use ordinary integer
  promotion, as in C. A suffix is therefore used only when it determines the
  operation's width, such as `1u64 << shift`, rather than after every byte value.
- An array is declared as `Entry[MAX_ENTRIES] entries;`: `Entry[MAX_ENTRIES]`
  is a single complete type suitable for fields, return types, and nesting in
  `o<T>` and other type constructors. The C form `Entry entries[MAX_ENTRIES]`
  splits the type around the identifier and is therefore not used in R.
- An empty native parameter list is written as `()`, for example `i32 main()`.
  In `extern "C"`, the same notation means exactly zero C parameters and is checked
  as the C prototype `(void)`.
- Source code has no lifetime parameters or suffixes. The compiler infers hidden
  regions from value flow and stores borrow-origin relations in interface metadata.
  In async code, a path/handle borrow ends when the standard operation returns its
  task; the async frame retains only owned/retained state. An apostrophe is used
  only as a C-style character-literal delimiter, not as part of type syntax.
- `ai8`, `ai16`, `ai32`, `ai64`, `aisize`, `au8`, `au16`, `au32`, `au64`, and
  `ausize` are exact shorthand forms of the corresponding `atomic iN`, `atomic isize`,
  `atomic uN`, and `atomic usize`; they are neither ordinary integer types nor literal suffixes.

Data flow:

```text
path_from_utf8(archive, output)
    -> start and await read_file
    -> start and await open_directory
    -> parse EOCD and central directory
    -> validate every local record and all path/range conflicts
    -> split Catalog into two ranges
    -> start and await the first extraction range
    -> start and await the second extraction range
    -> decode and CRC-check each entry into owned bytes
    -> create_directory_beneath for entries/parents
    -> atomic no-replace write beneath capability
```

## Building and running

The example requires the `hosted-native-async` profile; the target selects a full
native backend for filesystem and console operations. The module-map format and
driver command remain implementation-defined. The [`modules.map`](modules.map) file
provides an unambiguous mapping for adaptation to a particular compiler. Schematically:

```text
r build --module-map examples/unzip/modules.map --entry example.unzip.main
unzip archive.zip output-directory
```

Tests use the separate entry point `example.unzip.tests.main`; both entry points
must not be included in the same hosted program:

```text
r test --module-map examples/unzip/modules.map --entry example.unzip.tests.main
```

The current developer frontend already validates the reachable module graph and
builds the AST with this command:

```text
r-front --emit=ast \
  --module-map examples/unzip/modules.map \
  --entry example.unzip.main::main
```

The workspace already contains a reference frontend and strict-C17 backend. It
builds CST/AST/HIR/MIR, lowers async state machines, and links the required
`std.fs`/`std.io` operations to the Darwin runtime.
The `r_frontend_codegen_unzip_pure` CTest loads the reachable graph for
`example.unzip.tests.main`, generates strict C17, compiles it, and runs all four
DEFLATE/CRC tests. `r_frontend_codegen_unzip_full` generates, compiles, and runs the
complete async `unzip` against a ZIP fixture. An additional raw executable is used
by `r_frontend_zip_unzip_roundtrip` to extract randomized archives created by the
`zip` example, followed by a byte-for-byte tree comparison.

The output directory must already exist: `main.r` opens it once as
`std.fs::directory`, after which all archive paths are resolved only relative to
that capability.

## Why async writing remains safe

Validating a path string alone is insufficient: the filesystem can change between
validation and writing. Therefore, `host.r` uses capability-relative `*_beneath`
operations, safe component resolution, and atomic no-replace publication. Even if
two different UTF-8 strings match after platform-specific normalization, an existing
file will not be overwritten; one writer receives
`std.fs::error_code::already_exists`.

Directory creation is idempotent, so an explicit directory entry does not conflict
with automatic parent creation before a file write. ZIP metadata and permissions
are deliberately not applied.

Each filesystem operation receives an owned path or buffer before suspension. The
native backend retains the directory handle until terminal completion, so no
operation depends on borrowed stack storage and no blocking I/O worker pool is needed.

An immediate start followed by an await needs no intermediate task variable.
Examples of individual calls from `src/main.r`:

```r
await run(move archive_path, move output_path);
bytes archive_bytes = await read_archive(move archive_path);
```

When work or explicit task management is needed between start and await, retain
a named task and use `await move task_name`. Both forms handle start and completion
errors identically.

Diagnostics in `src/host.r` are built using `f"unzip: {error.message}\n"`.
The resulting `std.string::string` transfers its buffer through `std.string::into_bytes`
for async writing; allocation remains a checked operation.
