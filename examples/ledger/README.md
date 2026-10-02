# Synchronized ledger

Post signed transactions concurrently or hand a value to another native thread.
The program uses two posting lanes; the total is independent of their interleaving.

```sh
ctest --test-dir build-debug -R 'example_ledger|ledger_commands' --output-on-failure
build-debug/tests/codegen_example_ledger batch 100 -20 7 -3
build-debug/tests/codegen_example_ledger handoff 42
```

`batch` accepts signed 32-bit entries and accumulates a signed 64-bit balance and
entry count under a mutex. A two-participant barrier starts both lanes together.
The foreground tries the lock without blocking first and uses the blocking lock
operation on contention. The final snapshot borrows the balance through a read-only
guard. The scoped worker borrows the original input array; no copy of the entries
is needed. A failed thread start leaves nobody waiting at the barrier.

`handoff` sends a signed 64-bit value through a mutex-protected slot and waits for
acknowledgement. Condition-variable notifications are paired with a persistent
phase predicate, so notifications before a wait and spurious wakeups are harmless.
The sender notifies one receiver when the value is ready; the receiver acknowledges
and notifies all waiting observers. No polling sleeps are used.

Exit codes: 64 for usage, 65 for numeric input, 70 for a ledger synchronization failure
and 71 when the entry array cannot grow. Thread, barrier, allocation and async-start failures
reach the implicit `main` error boundary (116 or 112). The command tests
compare balances with Python integers and bound every subprocess with a timeout.
