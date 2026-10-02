# Message ingestion with arenas, a pool and budgets

Parse messages of `key=value;key=value` fields in batches: each batch is parsed by a worker task
into an arena of bytes that a pool of arenas lends it, inside a budget block of its own, and its
report reads the first message back from the arena and states what the budget was charged
(Library R-SLIB-ARENA-0001..0005, R-SLIB-POOL-0001..0003, R-SLIB-ALLOC-0004; Core R-STMT-0020).

```sh
ctest --test-dir build/debug -R 'example_ingest' --output-on-failure
build/debug/tests/codegen_example_ingest demo
printf 'device=boiler;value=21.5\nbroken\n' | build/debug/tests/codegen_example_ingest run 2 8192
```

`demo` takes two leases of a pool of two arenas, finds the pool empty, parses two batches at
once and a third batch whose budget is too small for its messages:

```text
pool: 2 arenas of 256-byte blocks, at most 1024 bytes each, 2 available
two leases: 0 available, a third acquire finds none: true
one lease dropped: 1 available
lines 1-4: 3 messages, 9 fields, 1 rejected
  first message: device=boiler metric=temperature value=21.5
  line 3 rejected: missing_equals at byte 0
  arena: 108 bytes stored in 256 reserved
  budget of 8192 bytes: 1440 charged, 6752 left
lines 5-8: 2 messages, 7 fields, 2 rejected
  first message: device=garage metric=temperature value=8.5
  line 6 rejected: empty_key at byte 14
  line 7 rejected: too_large at byte 14
  arena: 103 bytes stored in 256 reserved
  budget of 8192 bytes: 1248 charged, 6944 left
lines 9-11: the budget of 1000 bytes refused the batch (budget_exhausted) after 1 of 3 lines
after the batches: 2 available; outside every budget block budget_usage() is none: true
```

`run BATCH_LINES BUDGET_BYTES` reads messages from the standard input, one per line, and parses
them in batches of `BATCH_LINES` lines, two batches at a time, each in a budget of
`BUDGET_BYTES` bytes; a last line totals the batches.

[parse.r](src/parse.r) copies the key and the value of every field into the arena once and keeps
only their pieces: `std.arena::arena::store` appends to the current block and allocates a block
only when it is full. A part larger than the arena admits is refused with `limit_reached`, and
the line is rejected as `too_large`.

[batch.r](src/batch.r) is the worker. It takes an arena from the pool, parses inside a budget
and reads the usage of the budget while the parse still holds its results:

```r
budget (std.alloc::limits {.bytes = o::some(budget_bytes)}) {
    try {
        parsed outcome = parse_lines(lease.get_mut(), &lines, first_line, &done);
        core::replace(&charged, std.alloc::budget_usage()) as void;
        core::replace(&kept, o::some(move outcome)) as void;
    } catch (std.alloc::alloc_error failure) {
        refused = o::some(failure);
    }
}
```

The blocks of the arena and the arrays of the messages are charged to the budget; an allocation
beyond it fails with `budget_exhausted`, and the report counts the lines parsed before. The
worker resets the arena, which returns its blocks and their bytes, and the lease puts the arena
back into the pool when it is dropped.

[main.r](src/main.r) makes the pool from two arenas with `std.pool::pool<std.arena::arena>::create`
and gives each worker a handle of it with `share`.

The behaviour test checks the reports against a model of the parser and the arena written in
Python, over hand-written and random lines in batches of several sizes, and a budget smaller than
one block, which refuses every batch before its first message.
