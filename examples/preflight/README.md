# Service preflight checks

Validate configuration values before starting a service and translate failures to
a common diagnostic without losing the originating domain and code.

```sh
ctest --test-dir build-debug -R 'example_preflight|preflight_commands' --output-on-failure
build-debug/tests/codegen_example_preflight address 127.0.0.1
build-debug/tests/codegen_example_preflight utf8 c3a9
build-debug/tests/codegen_example_preflight boundary 'café' 4
build-debug/tests/codegen_example_preflight duration 0 1000000000
build-debug/tests/codegen_example_preflight barrier 0
build-debug/tests/codegen_example_preflight reserve 18446744073709551615
build-debug/tests/codegen_example_preflight thread
build-debug/tests/codegen_example_preflight asynchronous
build-debug/tests/codegen_example_preflight profile
build-debug/tests/codegen_example_preflight frame 'hello world'
build-debug/tests/codegen_example_preflight packet 'hello world'
```

Checks cover addresses, path construction, UTF-8 byte input, scalar boundaries,
bounded byte moves, duration representation, barrier participants, array capacity,
and decimal u32 input. `reserve` accepts at most 4096 elements or the exact maximum
usize sentinel used to exercise checked size multiplication, so it cannot request
arbitrarily large real allocations. Barrier checks construct and drop the barrier;
they do not wait for absent participants. Thread and async checks start actual
minimal workers and handle startup errors. Native resource-exhaustion branches are
not forced by consuming host resources.

A rejected value produces a domain/code/name report and succeeds as an inspection
command. Malformed command operands return 64 or 65. The memory needed to format a
diagnostic is checked separately; if that allocation fails, the implicit `main` error
boundary reports it with status 112.
Decimal overflow adds an actionable hint while preserving the original parse error.
The hexadecimal `utf8` command can examine invalid byte sequences that process
arguments cannot represent directly.

The `profile` command reports the compiled target and execution model, for example
`profile=hosted-native-async target=arm64-apple-darwin execution=threads+tasks`.
Module `@if` selects the policy declaration; statement `@if` selects whether that
policy includes native tasks. Inactive branches introduce no executable operations.
The policy query is checked with `@noalloc @nonblocking`; building and printing its
human-readable report can still allocate.

The `frame` command packs UTF-8 bytes into `Frame<64usize>`, including a four-byte
big-endian length header, and reports the used length and CRC32 of the wire bytes.
It accepts at most 60 input bytes and reports a checked capacity error before any
write on overflow. `@generic<const usize N>` gives each frame a fixed layout; `pack`
and `frame_crc` infer `N` from their borrowed frame argument. Both helpers have
checked `@noalloc @nonblocking` contracts. Only the final human-readable report
uses an owning formatted string.

The `packet` command checks a borrowed `View<u8>` with a closure defined inside a generic
function. Validation and checksum calculation have checked `@noalloc @nonblocking`
contracts, including the checksum callable constraint. `@must_use Summary` marks the byte
count and checksum as significant. The borrowed view ends before awaiting an async closure
that owns a Copy capture of that summary and produces the report. Input ownership, async
start and output allocation retain their ordinary checked errors. The 60-byte input limit
is checked before the report is produced; Unicode input is measured in UTF-8 bytes.
