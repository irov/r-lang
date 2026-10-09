# Tables and frame sizes computed during translation

An ordinary R function runs during translation when its arguments are known and its body
has no effect outside the call (Core R-FUNC-0023, R-EXPR-0032). No attribute marks such a
function; the compiler proves it from the checked body and the whole call chain.

```sh
ctest --test-dir build-debug -R tables_example --output-on-failure
```

`model.r` builds the CRC-32 lookup table with loops over `crc32_entry`:

```r
const u32[256] CRC32_TABLE = crc32_table();
```

The generated program holds the 256 values as the initializer of one global; `crc32_table`
itself does not run in the program. `main` compares the table-driven `crc32` with the library
`std.hash::crc32` at run time, so both implementations agree on every build.

A translation-time value can size a type declared at module scope:

```r
const usize FRAME_BYTES = frame_bytes(PAYLOAD_BYTES);

struct Frame {
    Header header;
    u8[PAYLOAD_BYTES] payload;
    u32 checksum;
};

enum Operation : u8 { ping = opcode(1u8), data = opcode(2u8), close = opcode(3u8), };
```

`frame_bytes` uses `sizeof(Header)` of the selected target, not of the host, and `main`
checks that `sizeof(Frame)` equals the computed size. Enumerator values come from `opcode`.

`checked_capacity` validates configuration: a zero, too large or non-power-of-two capacity
stops the build with `R-DIAG-CONST-003` and the panic message, for example

```text
error R-DIAG-CONST-003 [R-EXPR-0032]: translation-time evaluation panics
(explicit: "queue capacity must be a power of two") in `checked_capacity`
```

A constant condition in `@if` selects declarations or statements during translation
(Core R-META-0002):

```r
@if (QUEUE_CAPACITY * sizeof(u32) <= 4096usize) {
    struct Queue { u32[QUEUE_CAPACITY] slots; usize head; };
} @else {
    struct Queue { array<u32> slots; usize head; };
}
```

The condition is an ordinary comparison over constants and translation-time calls. Only the
selected branch is checked and generated, so the queue of 1024 slots keeps them inline, and
`wrap` compiles to a mask because the capacity is a power of two. Inside a generic function a
condition over `const usize N`, `sizeof(T)` or a generic call such as `size_of::<T>()` is decided
for each instantiation.

A trait can declare associated constants of an integer type or `bool` (Core R-TYPE-0050).
Each implementation binds them, a default may use `Self::NAME`, and generic code reads them for
each closed type:

```r
trait Message {
    const u8 OPCODE;
    const usize PAYLOAD;
    const bool ACKNOWLEDGED = false;
};

impl Message for Data {
    const u8 OPCODE = opcode(2u8);
    const usize PAYLOAD = PAYLOAD_BYTES;
    const bool ACKNOWLEDGED = true;
};

@generic<M: Message>
struct Packet {
    Header header;
    u8[M::PAYLOAD] payload;
};
```

`Packet<Ping>` carries 8 payload bytes and `Packet<Data>` carries 48; `packet::<M>()` fills the
header from `M::OPCODE` and chooses the version with `@if (M::ACKNOWLEDGED == true)`.

A function that reads thread-local or mutable module state, allocates, performs I/O or
calls such a function stays a run-time function. Where a constant is required, the
diagnostic `R-DIAG-CONST-002` names the first reason along the call chain. Elsewhere a
call whose evaluation would panic or exceed the translation limits simply runs at run time.
