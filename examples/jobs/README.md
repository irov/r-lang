# Background job supervisor

Deduplicate job IDs, select a shard, and manage a checksumming task's lifetime.

```sh
ctest --test-dir build-debug -R 'example_jobs|jobs_commands' --output-on-failure
build-debug/tests/codegen_example_jobs dispatch 7 12 7 25
build-debug/tests/codegen_example_jobs wait 10 'payload'
build-debug/tests/codegen_example_jobs detach 10 'payload'
build-debug/tests/codegen_example_jobs cancel 1000 'payload'
```

`dispatch` uses `JobKey<T>`, whose generic `hash`/`equal` hooks come from `@derive(key)`: they
hash and compare the key field. The dictionary rejects duplicate IDs through those hooks, and
`core::hash` chooses one of four
shards for each accepted ID. Hash values are implementation details, not a wire
format or a persistent identifier; do not use these shards across independently
versioned programs without defining a separate stable partitioning contract.

The other commands transfer a byte buffer through `Lease<T>`. Its generic `drop`
hook increments an atomic release counter; automatic field cleanup then destroys
the payload and shared audit reference. The job waits on a timer, computes CRC32,
and increments a separate `finally` counter. `wait` consumes the named task,
`detach` releases the task handle while work continues, and `cancel` requests
cancellation after the worker has entered its body. The supervisor retains a
separate audit owner and waits for payload release, so every command reports the
actual cleanup before returning.

Cancellation races completion: a completed job may publish its checksum before the
request arrives. A zero checksum alone cannot distinguish cancellation from empty
input. The report therefore keeps cleanup counters separate from the checksum and
internal error flag. Delays are limited to five seconds. Polling yields through
an awaited timer; it does not block a runtime worker with a busy wait.
