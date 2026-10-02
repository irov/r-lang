# Blocking C calls beside running tasks

Run a blocking C function without stopping the other tasks: `std.async::blocking` (Library
R-SLIB-ASYNC-0017) starts a direct entry on the blocking call pool of the runtime (Core
R-TERM-0015), a few threads that are separate from the executor. The entry here wraps libc
`usleep`, which suspends the thread that calls it.

```sh
ctest --test-dir build-debug -R 'example_offload' --output-on-failure
build-debug/tests/codegen_example_offload nap 30
build-debug/tests/codegen_example_offload cancel 120
```

`offload nap MILLISECONDS` starts six calls of that length at once beside a ticker task that
counts one tick per millisecond:

```text
6 calls of 30 ms slept 180 ms
the ticker kept running: yes
two rounds on four pool threads: yes
a call runs as a task of its own: yes
```

The pool has the four threads that the target manifest records. Four calls run at once, the
other two wait in the queue for a free thread, so the six calls take at least two rounds. The
ticker never waits for them: the calls block pool threads, never an executor worker.

[native.r](src/native.r) imports `usleep` and wraps it in an ordinary safe function, the entry.
An entry is a direct module-level R function, like the entry of `std.thread::spawn`; its
arguments are Send and move into the call:

```r
u32 nap(u32 milliseconds) {
    unsafe {
        c_int status = usleep((milliseconds * 1000u32) as c_uint);
        status as void;
    }
    return milliseconds;
}
```

[main.r](src/main.r) starts the calls as members of a task group. The call is a task, so it is
awaited, cancelled, detached or selected like any other; its result is the entry's result:

```r
auto a = std.async::blocking(example.offload.native::nap, ms);
...
slept += (await move a) + (await move b) + (await move c);
```

`std.async::task_id` (Library R-SLIB-ASYNC-0018) shows it: the entry that the pool runs reports
the identifier of the task of its call, which starts after the task that awaits it and so has a
larger identifier:

```r
u64 here = std.async::task_id();
u64 pooled = await std.async::blocking(current_task, 0u64);
std.string::string separate = answer(pooled > here);
```

`offload cancel MILLISECONDS` races one call against a timer of ten milliseconds. The timer
wins the select and the group cancels the call, but a call that a pool thread has taken is not
interrupted: the task reports cancellation only after the call returns, so the group ends after
the whole nap. A call still waiting in the queue would be removed at once.

```text
the timer won: yes
the group waited for the call to return: yes
```

To emit the program manually, supply both maps and the link manifest:

```sh
build-debug/r-front --module-map examples/offload/modules.map \
  --entry example.offload.main --library-map library/r/library.map \
  --link-manifest examples/offload/link_manifest.json --emit=c17 > /tmp/offload.c
```
