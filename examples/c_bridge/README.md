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
build-debug/tests/codegen_example_c_bridge loadable libSystem.B.dylib
build-debug/tests/codegen_example_c_bridge signal 5
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

`loadable` opens a shared library while the program runs (`std.dl::open`, by name or by path)
and calls its `labs` through `std.dl::resolve`: the `extern "C"` block that declares `labs` has
the link kind `loadable`, so the link step never names the library, and `<stdlib.h>` verifies the
prototype the resolved function pointer carries. It prints `labs(-42)=42 strlen=true` for the
system library (`std.dl::symbol` finds `strlen` as well); a library the loader refuses exits with
69 (`library not loaded`), an empty path with 64.

`signal COUNT` installs `on_user_signal`, an `extern "C"` function with `@signal_handler`, for
SIGUSR1 with the imported `signal`, and raises the signal COUNT times. The compiler proves the
handler and the `@signal_safe` function it calls: no allocation, no blocking, no `thread_local`
or runtime state, no value with cleanup, and only imports marked `@signal_safe` (`raise`). A
panic there would end the process with its report instead of unwinding through the interrupted
code. The raising runs on a thread of the blocking pool (`std.async::blocking`): the threads of
an async program start with the signal blocked, so this one unblocks SIGUSR1 with
`pthread_sigmask` and restores its mask afterwards. Each `raise` then runs the handler on the same
thread before it returns. The handler writes two plain mutable statics and ends with
`core::signal_fence(release)`; the raising thread reads them after
`core::signal_fence(acquire)`, which orders a thread with its own signal handlers. It prints
`raised=N handled=N synchronous=N signal=30` (SIGUSR1 is 30 on the target) for counts up to 1000.

The raw-pointer contracts describe allocation, length, alignment and lifetime.
`core::assume` repeats a bound already enforced before creating the packet; it does
not validate untrusted lengths. The callback entry trampoline also provides the
ordinary C-to-R attachment guarantee. `attachment` acquires an explicit attachment,
checks that a duplicate acquisition is rejected, and releases the first attachment.
A callback must reuse its trampoline context rather than attach a second time. `target` prints the selected C capabilities
and manifest availability, rather than probing arbitrary installed libraries.

To compile the program manually, supply both maps and the link manifest. With `--sysroot`,
r-front parses the headers of the imports with libclang in that SDK and proves each imported
prototype against them before it writes the object; the object goes to standard output and calls
`strlen` and the other imports directly. The CTest case `r_frontend_codegen_example_c_bridge`
builds it the same way and links it with the runtime and the library into
`build/debug/tests/codegen_example_c_bridge`:

```sh
build/debug/r-front --module-map examples/c_bridge/modules.map \
  --entry example.c_bridge.main --library-map library/r/library.map \
  --link-manifest examples/c_bridge/link_manifest.json \
  --sysroot "$(xcrun --show-sdk-path)" --emit=object > /tmp/c_bridge.o
```
