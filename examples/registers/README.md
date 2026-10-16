# Register and ticket workbench

Apply a script to a 32-bit atomic control word, issue tickets concurrently,
simulate volatile device-register access, hand messages between threads through
memory fences, measure the stack, write a register block in static storage,
measure a register block through its addresses, or halt the process with the
trap instruction.

```sh
ctest --test-dir build-debug -R 'example_registers|registers_commands' --output-on-failure
build-debug/tests/codegen_example_registers script 0 bit_or 3 bit_xor 1 cas 2 9 add 4
build-debug/tests/codegen_example_registers race 10000
build-debug/tests/codegen_example_registers device 0 3 1 9
build-debug/tests/codegen_example_registers handoff 2000
build-debug/tests/codegen_example_registers stack 17
build-debug/tests/codegen_example_registers map 5 1073741831 2147483657
build-debug/tests/codegen_example_registers watchdog 10 1 2 3
build-debug/tests/codegen_example_registers layout 5
```

`script` supports `add`, `sub`, `bit_and`, `bit_or`, `bit_xor`, `exchange` and
`cas EXPECTED DESIRED`. Each operation reports the observed previous value and the
result. CAS distinguishes accepted and stale expected values. Unsigned atomic
arithmetic wraps modulo 2^32. The final line reports the control word and whether
its atomic representation is lock-free on the target.

`race` has two native threads issue sequential tickets from one atomic counter.
Each accumulates its tickets locally; joining combines their sums. For N tickets,
the expected sum is N(N-1)/2. Each lane accepts at most 100000 iterations. This
checks the shared counter without depending on a particular scheduling order.

`device` reads and writes an aligned local word through raw pointers and the
volatile intrinsics. It models register access without touching hardware.
Volatile access does not provide synchronization; this mode uses one thread.
The pointers do not outlive the local word.

`handoff N` passes N values (3, 6, 9, ...) from a producer thread to the main
thread through a one-slot mailbox of two relaxed atomics. A release fence before
the store of the full state and an acquire fence after its load order the value
with the state (`core::atomic_fence`); the same pair in the other direction lets
the producer reuse the slot. It prints `messages=N sum=S` for N up to 10000.

`stack DEPTH` descends DEPTH levels of a function with `@recursion(depth = 256)`
and compares `core::stack_pointer()` between levels: each deeper level has a
lower, 16-byte aligned stack pointer, and the frame record
(`core::frame_address()`) lies above the stack pointer. It prints
`depth=D grows_down=true frame_above_stack=true` for depths up to 200.

`map WORD...` treats each word as a command: the two top bits select one of four
registers (`control`, `status`, `data`, `scratch`) of a block in static storage,
the low 30 bits are the value. The register map is a module object whose
initializer holds the addresses of the block's fields (`&device.data as raw
u32*`): address constants, relocated in the program image, which need no unsafe
context. Each write prefetches the register, reads it, stores the value and
reads it back with the volatile intrinsics; the last line lists the four
registers. The register index is proven below four, so the path that would not
be is `core::unreachable()`.

`watchdog LIMIT VALUE...` adds the values and halts the process with
`core::trap()` as soon as the total exceeds LIMIT: the process ends by SIGTRAP
(exit status 133 in a shell), without unwinding or output. Otherwise it prints
`total=T limit=L`.

`layout COUNTER` measures a block of three registers from the addresses of its
fields (`core::pointer_address`), then writes COUNTER through the byte offset of
the counter from a read-only view of the block (`core::offset_bytes`, made
writable with `core::cast_mutable`) and reads the flags back by a negative
offset from the counter:

```text
status@0 counter@8 flags@16 size=24
counter=5 flags=2
```

Command tests use a Python bitwise/arithmetic interpreter, stale and successful
CAS operations, repeated concurrent ticket runs, volatile write sequences,
repeated fence handoffs, stack descents, register maps decoded in Python, trap
exits, block layouts and invalid input. Runtime-generated cleanup remains part of the normal
example build.
