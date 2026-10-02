# Worker pipelines

Small native-thread tools for message checksums, parallel sums and notification.
All commands print their result to standard output; run without arguments for usage.

```sh
build-debug/tests/codegen_example_workers queue hello world
build-debug/tests/codegen_example_workers bounded 1 hello world
build-debug/tests/codegen_example_workers detached hello world
build-debug/tests/codegen_example_workers scoped 10 20 30 40
build-debug/tests/codegen_example_workers owned 10 20 30 40
build-debug/tests/codegen_example_workers park 10
```

Use the executable path printed by the example build in your build directory.
`queue`, `bounded` and `detached` report each message's index, UTF-8 byte length and
CRC32. Ordering follows the single producer. Capacity zero is a rendezvous channel;
positive capacities buffer that many records. Capacity is limited to 4096.
The producer recovers unsent records when a channel is full or disconnected.
Dropping the receiver lets a blocked detached producer finish on cancellation.

`scoped` divides unsigned 32-bit inputs between two scoped threads and prints a
64-bit sum. Both workers borrow the original array, and the scope guarantees that
no worker outlives it. `park` waits for a child to publish an atomic ready flag and
unpark the parent. Its loop tolerates spurious wakeups. Delay is in milliseconds,
from zero to 1000.

`owned` captures a list by value in a `fn once` closure, passes it to the generic
async `execute` function, then transfers the pending task
to a reporting stage that awaits its task parameter. The input list is destroyed
by the calculation; the report independently owns its formatted result. Each
successful start transfers ownership once. A start error preserves its named Move
arguments for the caller's normal cleanup. This command has the same sum contract
as `scoped`, with an owning async pipeline instead of scoped input borrows.

The borrowed `sum` worker has checked `@noalloc @nonblocking` contracts. The generic
closure dispatcher keeps the callable mode, Send and unborrowed requirements explicit.
Starting the async task allocates a frame; formatting and worker orchestration remain
outside the resource contracts of `sum`.

The program demonstrates native thread creation, joining, detaching, scoped
borrows, channel factories, cloned senders, blocking and nonblocking operations,
Move outcome patterns, and atomic acquire/release synchronization. It handles the
`panicked` join variant for runtimes with an unwind strategy; the current default
runtime uses abort and does not turn a process panic into a recoverable result.

Behavior checks run real workers and compare checksums and sums with Python:

```sh
ctest --test-dir build-debug -R 'example_workers|workers_commands' --output-on-failure
```
