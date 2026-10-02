# C string and callback bridge

Use C strings, call the system C runtime, and hand an owned packet through an
opaque handle and a C function pointer. The example's link manifest selects the
project's supported Darwin/arm64 C ABI and declares the system `strlen` provider.

```sh
ctest --test-dir build-debug -R 'example_c_bridge|c_bridge_commands' --output-on-failure
build-debug/tests/codegen_example_c_bridge target
build-debug/tests/codegen_example_c_bridge attachment
build-debug/tests/codegen_example_c_bridge inspect 'Hello, C'
build-debug/tests/codegen_example_c_bridge upper 'Hello, C'
build-debug/tests/codegen_example_c_bridge release 'Hello, C'
build-debug/tests/codegen_example_c_bridge bytes 68656c6c6f00ff
build-debug/tests/codegen_example_c_bridge cstring 68656c6c6f
```

`inspect` creates an owning NUL-terminated C string, obtains bounded and raw views,
validates/copies its UTF-8 payload, and measures it with the imported C `strlen`.
`bytes` decodes hexadecimal input and views it as C characters of the same storage, a
view anchored to the decoded bytes: it requires a terminator, validates only the bytes
preceding its first zero, and ignores trailing bytes.
`cstring` validates hexadecimal bytes as R UTF-8 and then constructs a C string;
embedded NUL is rejected by that constructor. These two modes make the boundary
contracts visible on arbitrary binary input, including empty storage.

`upper` copies up to 256 UTF-8 bytes into an owned packet, releases its R owner into
an opaque handle, and calls an `extern "C"` function through a typed raw function
pointer. The callback trampoline supplies its runtime context. Its helper uppercases ASCII
bytes through a bounded mutable raw view; non-ASCII bytes stay
unchanged. R code fills and reads the payload through views anchored to the handle
(`core::slice_from_raw_parts_in_mut` and `core::slice_from_raw_parts_in`): the handle keeps
the packet alive, and while such a view is live the compiler rejects releasing or dropping
the handle, so validation always happens before the packet is destroyed. `upper`
drops the handle; `release` transfers the cleanup obligation and calls the destructor
explicitly. The destructor reacquires and drops the original R allocation.

The raw-pointer contracts describe allocation, length, alignment and lifetime.
`core::assume` repeats a bound already enforced before creating the packet; it does
not validate untrusted lengths. The callback entry trampoline also provides the
ordinary C-to-R attachment guarantee. `attachment` acquires an explicit attachment,
checks that a duplicate acquisition is rejected, and releases the first attachment.
A callback must reuse its trampoline context rather than attach a second time. `target` prints the selected C capabilities
and manifest availability, rather than probing arbitrary installed libraries.

To emit the program manually, supply both maps:

```sh
build-debug/r-front --module-map examples/c_bridge/modules.map \
  --entry example.c_bridge.main --library-map library/r/library.map \
  --link-manifest examples/c_bridge/link_manifest.json --emit=c17 > /tmp/c_bridge.c
```
