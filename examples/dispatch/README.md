# Delivery dispatcher

Plan an outward or return route, assign urgent jobs, or reconcile repeated booking
requests. The programs use the source-defined generic containers for different
views of the same practical problem.

```sh
ctest --test-dir build-debug --output-on-failure -R '(codegen_example_dispatch|example_dispatch_commands)'
build-debug/tests/codegen_example_dispatch route 1 101 205 310 420
build-debug/tests/codegen_example_dispatch returning 0 101 205 310
build-debug/tests/codegen_example_dispatch priority 3 20 80 80 150 -5
build-debug/tests/codegen_example_dispatch roster 205 310 101 205 310 101
```

- `route OMIT_LAST STOP...` visits input stops in order after removing the requested
  number of trailing stops. `returning` reverses the route first. `cancel` clears
  all pending stops. First/last observations borrow the deque; pops transfer values.
- `priority LIMIT URGENCY...` clamps urgencies to 0–100 and assigns up to `LIMIT`
  jobs. Input positions are job IDs. A custom `Ordered` implementation selects
  higher urgency first, with earlier input winning ties. The report includes the
  remaining work and its observed minimum/maximum urgency.
- `roster WITHDRAWN_ID REQUEST_ID...` combines duplicate requests, withdraws one ID,
  shows remaining IDs in arrival order, and prints reservation counts in sorted
  order. `pause` clears the dispatchable arrival set while retaining bookings for
  later resumption. A missing withdrawn ID leaves the roster unchanged.

Empty input lists and zero limits are valid. Invalid numeric input exits 65;
invalid commands or excessive omission exit 64; a container that cannot grow exits 71.
Other allocation failures reach the implicit `main` error boundary (112).

`main` keeps the six list commands in a `dict<str, fn(const i32[], str) -> ...>` built from a
dict expression: each entry is a function value (Core R-TYPE-0054) with the same parameters,
result and checked errors, and the command name selects the function to call instead of a
chain of comparisons. An unknown name is reported before any input is parsed.

The example exercises every public method of `std.deque::deque`, `std.heap::heap`,
`std.set::set` and its iterator, `std.sorted::set`, and `std.sorted::map`, plus all
five comparison helpers. Deque rebalancing is called only when the destination
half is empty. It also demonstrates traits, generic inference, Copy job records,
borrowed optional payloads and exhaustive switches.

The `message` command runs an owned message through an asynchronous retry policy:

```sh
build-debug/tests/codegen_example_dispatch message 0
build-debug/tests/codegen_example_dispatch message 0 'created order 42'
build-debug/tests/codegen_example_dispatch message 2 'refresh inventory'
build-debug/tests/codegen_example_dispatch message 4 'charge invoice'
```

`Message::Status` reports readiness. `Submit` contains an owned body and an optional
retry number. A nested `match` pattern recognizes attempts beyond three and formats
an expiry report. Other arms move the body into the asynchronous step of the `Delivery`
policy and await its result: `main` stores `deliver` for a first submission and
`redeliver` for a retry as async function values in the struct. Zero denotes an initial
submission; an absent text with zero requests status.

The guard inspects aliases before any payload moves. Only a selected arm transfers
ownership, and the remaining payload is destroyed when that arm ends. The complete
`match` is the return expression, so each arm supplies one owned report. This models
the routing policy locally; connecting a broker is outside this example.
