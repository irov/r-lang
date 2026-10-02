# Register and ticket workbench

Apply a script to a 32-bit atomic control word, issue tickets concurrently, or
simulate volatile device-register access using local storage.

```sh
ctest --test-dir build-debug -R 'example_registers|registers_commands' --output-on-failure
build-debug/tests/codegen_example_registers script 0 bit_or 3 bit_xor 1 cas 2 9 add 4
build-debug/tests/codegen_example_registers race 10000
build-debug/tests/codegen_example_registers device 0 3 1 9
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

Command tests use a Python bitwise/arithmetic interpreter, stale and successful
CAS operations, repeated concurrent ticket runs, volatile write sequences and
invalid input. Runtime-generated cleanup remains part of the normal example build.
