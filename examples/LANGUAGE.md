# Language guide to the applications

This is a navigation map, not a declaration of complete language conformance. Public function
coverage is in [coverage.json](coverage.json). [syntax-coverage.json](syntax-coverage.json)
separately counts active parser nodes in each application's own sources; imported standard
library implementations do not count as application syntax. Parser bookkeeping, recovery and
inactive node tags are excluded. A node's presence does not establish all of its ownership,
typing or control-flow cases.

## Current application routes

| Topic | Useful program to read | What to observe |
|---|---|---|
| Modules, imports, visibility | [ZIP](zip/README.md), [unzip](unzip/README.md) | A multi-module binary format tool with shared models and internal helpers |
| R and C scalar types | [Calculator](calculator/README.md), [numbers](numbers/README.md) | Exact numeric parsing, explicit conversions, every concrete arithmetic specialization |
| Fixed arrays, slices, byte order | [Binary](binary/README.md) | Telemetry packet encoding and fixed-width device labels |
| Structs and enum dispatch | [Workspace](workspace/README.md) | Public native records and commands selected from textual enum names |
| Function values | [Dispatch](dispatch/README.md) | A `dict` of function values selects a command by name; a struct holds async handlers (Core R-TYPE-0054) |
| Checked errors, conditional throw | [ZIP](zip/README.md), [calculator](calculator/README.md) | Payloads with diagnostic context and domain-specific command failures |
| Exact error recovery and assignment | [JSON workbench](json_tool/README.md) | Keep the original owning configuration when replacement decoding fails |
| Error families and the standard error root | [Settings](settings/README.md) | `error range_error : setting_error { ... }` derives from a base whose fields come first; `throws setting_error` and `catch (setting_error failure)` cover every member, `throw move failure` in another function keeps the exact error for its clauses, and `catch (std.error::fault failure)` receives any standard failure, reported through `std.error::from_fault` |
| Copy/Move, explicit drop, ordinary cleanup | [Snapshots](snapshots/README.md), [playlist](playlist/README.md) | Shared versions, unique access, removal returning owners and stable list borrows |
| Shared and mutable borrowing | [Warehouse](warehouse/README.md), [dispatch](dispatch/README.md) | End a view before updating its container; preserve independent node identities |
| Borrows in containers and results | [Word statistics](wordstats/README.md), [statistics](statistics/README.md) | `array<Entry>` of structs holding views of the command-line words, counted through `std.array::get_mut` and sorted in place through an `array<Entry>*` parameter; `array<u32*>` of exclusive borrows of counters and `list<const Entry*>` of shared borrows of entries; an exclusive borrow result is written through directly; a `never` function stands where a value is expected; runs are kept as `array<const i32[]>`, and a scoped task takes a query holding a view |
| Tuples and type packs | [Tally](tally/README.md) | A summary returned as one tuple and destructured into four locals with `auto (entries, sum, low, high) = ...`; columns of different types rendered by a function over a type pack, whose recursion spreads the pack and ends at `len(Tail...) == 0` |
| Method chains and builders | [Methods](methods/README.md) | A fence order built by `@chain` methods, `FenceOrder::around(14.0).with_height(1.5).with_gates(2u32).cost(10.0)`, where each link takes the order and returns it |
| Standard and custom ordering | [Dispatch](dispatch/README.md) | A custom `Ordered` implementation gives stable priority scheduling |
| Exchange and cloning | [Tournament](tournament/README.md) | `core::swap` flips a home side; `core::clone` copies league rows through a `Standing::clone` hook; `std.slice::sort_by`, `rotate_left` and `rotate_right` rearrange rows that own strings |
| Translation-time evaluation | [Tables](tables/README.md) | A CRC-32 table, a frame size used by a module-scope struct, enumerator values and a checked configuration computed by ordinary functions |
| Explicit generic arguments | [JSON workbench](json_tool/README.md), [generics](generics/README.md) | `canonical::<Configuration>` names a type parameter that occurs only in the body |
| Iteration, associated items, closures | [Statistics](statistics/README.md) | Transform measurements and page reports through iterator adapters |
| Generic schemas and constraints | [JSON workbench](json_tool/README.md) | `Snapshot<T>` requires `json_encode & json_decode` and is instantiated for a configuration |
| Strings, Unicode and regular expressions | [Text](text/README.md) | Search, replace, split and validate text at byte/UTF-8 boundaries |
| Compression and resumable stream coders | [Deflate](deflate/README.md) | DEFLATE, zlib and gzip over caller-owned buffers, flush modes, output limits and checked stream errors |
| Markup and streaming event parsers | [XML](xml/README.md) | Pull events over fragments, namespace resolution, streaming path selection and escaped output |
| Captured and positional formatting | [Receipt](receipt/README.md) | Reuse a captured template and repeat argument positions |
| Formatting records through `core::Format` | [Status](status/README.md) | `@derive(format)` records and enums, an own implementation, a column bounded by `core::Format`, widths in characters, socket addresses and `array<own dyn(core::Format & send)*>` |
| Async functions and direct await | [Byte pipe](bytepipe/README.md), [clock](clock/README.md) | Task results become initialized only after completion; partial writes advance an offset over a loaned view |
| Scoped I/O over loaned buffers | [Byte streams](streams/README.md), [byte pipe](bytepipe/README.md) | `read_into`, `write_from` and their file and socket forms borrow caller storage as task-group loans that end at backend acknowledgement |
| Asynchronous iteration | [TCP service](service/README.md), [relay](relay/README.md) | `for (u64 amount in &events)` awaits each amount of a `std.sync::receiver` until the last sender is gone; `NumberedLines` implements `core::AsyncIterator`, whose `@scoped async next` the loop starts in the enclosing `task_scope` at each iteration |
| Pattern tests in conditions | [HTTP](http/README.md), [environment](environment/README.md) | `while (parser->next() is variant o::some(move message))` drains a parser until `o::none`; `if (std.env::get(name) is variant o::some(move value))` binds a present variable |
| Loop labels | [network lab](netlab/README.md) | The `end` clause of a read `switch` leaves the labeled loop with `break reading;` instead of a completion flag |
| Struct update | [testing](testing/README.md) | `version {.patch = current.patch + 1u32, ...current}` takes the other parts from the current version |
| Borrowed standard outcomes | [assistant](assistant/README.md) | `switch (finished)` reads the exit code of a `std.process::wait_result` place without consuming it |
| Stream traits and buffered I/O | [Relay](relay/README.md) | `std.stream::Reader`, `Writer` and `Stream` over standard streams, files, TCP connections and child pipes; one line protocol through `own dyn(std.stream::Stream)*`; `std.bufio` lines, delimited fields, exact reads and a writer flushed explicitly; `std.console` prompts and line input |
| Encrypted streams | [TLS](tls/README.md) | `std.tls::stream<S>` over any `std.stream::Stream`, `connect` and `accept` in one task group, ALPN and version of a session, `std.tls::tls_error` codes of rejected certificates; a standard module written in R over a native provider through the checked C boundary |
| Binary items, signatures and sealing | [Notary](notary/README.md) | `std.cose::sign1` signs with an Ed25519 key stretched from a passphrase by Argon2id, `std.cbor::diagnostic` shows the message, and `std.cbor::encoder` writes a map in deterministic key order that `decode_deterministic` reads back |
| Relational storage and transactions | [Device registry](registry/README.md) | A device and the outbox event that announces it are written in one `BEGIN IMMEDIATE` transaction; `std.sqlite` operations copy their text and return a started task, so `publish(&db, ...)` is an ordinary function that returns the task of its insert |
| Observable services | [Telemetry](telemetry/README.md) | One router served on TCP, TLS and a Unix socket by `std.http::serve_all`; handlers update `std.metrics` handles kept in an `arc` state and finish `std.trace` spans; SIGTERM drains the service through `stop_on_signals` |
| Arenas, pools and budgets | [Ingest](ingest/README.md) | A parser stores keys and values in a `std.arena::arena` and keeps only `std.arena::piece` values; workers lease arenas from a `std.pool::pool`, parse inside `budget (std.alloc::limits {...})` and read `std.alloc::budget_usage()` before the lease puts the arena back |
| File locks and positional I/O | [Journal](journal/README.md) | An update holds `data.lock(std.fs::lock_kind::exclusive, 0u64, 0u64)` inside a `deadline` block, writes its journal entry with `write_all_at_from` and orders it with `sync(std.fs::sync_level::barrier)` before `write_all_at` writes the record at its offset; a read locks one record's range; `scan` reads the store through `std.fs::map_file`, whose `bytes()` is a view anchored to the mapping (`core::slice_from_raw_parts_in`) |
| A database client written in R | [Orders](orders/README.md) | `std.postgres` speaks the PostgreSQL protocol over `std.net` and `std.tls`: a connection is an `arc` handle whose operations return tasks and take turns under an async mutex, so two tasks place orders through `share()` handles while a third connection waits in `wait_notification`; `std.postgres::cancel` stops a statement from another task |
| HTTP services and clients | [HTTP service](http/README.md) | Async function items as route handlers over `arc` shared state, function values as hooks, one router served over plain and TLS streams, an owned client with a pool of `own dyn(std.stream::Stream)*` connections |
| Atomics and volatile access | [Register workbench](registers/README.md) | Apply control-word operations, issue tickets concurrently and simulate a volatile register |
| Lock guards and condition variables | [Ledger](ledger/README.md), [price quotes](quotes/README.md) | Protect transactions, wait on predicates, and publish read/write snapshots |
| Native threads and synchronization | [Worker pipelines](workers/README.md) | Send checksummed records through channels, join scoped sums and wait for an atomic notification |
| Concurrent named tasks | [Runner](runner/README.md), [network laboratory](netlab/README.md) | Read stdout/stderr concurrently or receive while another task sends |
| Select, deadlines and explicit cancellation in task groups | [Fanout](fanout/README.md) | Consume whichever analysis finishes first under a time budget; unselected tasks are cancelled only by `cancel_all` or the group exit |
| A service built on `std.service` | [TCP service](service/README.md) | Handlers own their connection and throw any standard error; the service bounds them by capacity and a connection timeout, lets a connection beyond the capacity wait for a free handler slot (`overflow::wait`), shares an `arc` state and counts their outcomes, and a stop channel drains it; every helper declares the root `std.error::fault` |
| Host environment and process ownership | [Environment](environment/README.md), [runner](runner/README.md) | Owned argument snapshots, launch policy, child lifetime and completion records |
| Binary buffer adoption and extraction | [Buffer comparison](buffer_compare/README.md) | Transfer an allocator-backed owner through raw ownership tokens |
| JSON contracts and hooks | [JSON workbench](json_tool/README.md) | Storage stays independent from optional input fields, null and omission rules |
| Collection expressions, ranges and membership | [Tournament](tournament/README.md) | Generate unique match pairs and count appearances with arrays and dictionaries |
| Traits, associated types and constants, explicit implementations | [Tournament](tournament/README.md) | A generic draw projects `Draw::Entry` and checks `Draw::CAPACITY` through an explicit implementation of `Draw` |
| Derived implementations | [Tournament](tournament/README.md), [background jobs](jobs/README.md) | `@derive(equal)` gives `Player` the `Equal` that registration search uses; `@derive(equal, ordered, key)` makes `Pairing` a `std.set::set` element compared in the derived order; `@derive(key)` defines the hash and equal hooks of the generic `JobKey<T>` that keys a dictionary |
| Dyn interfaces over a closed set of implementations | [Keystore](keystore/README.md) | A backend chosen by configuration is owned once as `own dyn(Store)*` in a session and serves every command through `dyn(Store)*` |
| Callable constraints, move captures and spread | [Tournament](tournament/README.md) | Own a scoring policy, apply it generically and spread results into a variadic total |
| Bounded recursion | [Calculator](calculator/README.md) | `sum`, `product` and `factor` of the expression parser call each other with `@recursion(depth = 16)`; deeper nesting throws `core::recursion_error` before the call, and the stack bound counts 16 frames of each |
| Variadic views and a spread subrange | [Calculator](calculator/README.md) | `calculate(str... words)` receives the words after the program name through `calculate(...arguments[1..])` without copying them, and forwards the operands to the evaluator of the chosen type with `...operands` |
| Generic key and drop hooks | [Background jobs](jobs/README.md) | Deduplicate typed keys through derived key hooks and release an owned payload with an observable cleanup counter |
| Cancellation, detachment and finally | [Background jobs](jobs/README.md) | Supervise actual task completion and retain audit state after the task handle is consumed |
| Common diagnostics and typed error codes | [Preflight](preflight/README.md) | Preserve a rejected value's error domain and provide an actionable overflow hint |
| Reflection and owning enum variants | [Logbook](logbook/README.md) | Filter structured events and report enum names, ordinals and schema fields |
| C ABI imports, callbacks and raw function types | [C bridge](c_bridge/README.md) | Verify `strlen` against its header; pass a packet through a raw callback and destructor |
| Program termination | [Process status](process_status/README.md) | Synchronous exit and deliberate abnormal termination observed by a supervisor |

## Source-defined container methods

The builtin API inventory does not enumerate these methods. The following method calls appear
in [dispatch](dispatch/README.md), and the command tests verify their observable collection
behavior. Receiver types are explicit in the linked sources.

| Receiver | Methods demonstrated | Source |
|---|---|---|
| `std.deque::deque<T>` | `create`, `count`, `is_empty`, `push_front`, `push_back`, `rebalance_front`, `rebalance_back`, `pop_front`, `pop_back`, `front`, `back`, `clear` | [routes.r](dispatch/src/routes.r) |
| `std.heap::heap<T>` | `create`, `count`, `is_empty`, `peek`, `push`, `pop` | [priority.r](dispatch/src/priority.r) |
| `std.set::set<T>` | `create`, `insert`, `contains`, `remove`, `count`, `is_empty`, `clear`, `iter` | [roster.r](dispatch/src/roster.r) |
| `std.set::set_iter<T>` | `next` | [roster.r](dispatch/src/roster.r) |
| `std.sorted::set<T>` | `create`, `count`, `lower_bound`, `contains`, `insert`, `remove`, `as_slice` | [roster.r](dispatch/src/roster.r) |
| `std.sorted::map<K,V>` | `create`, `count`, `lower_bound`, `get`, `contains`, `insert`, `remove` | [roster.r](dispatch/src/roster.r) |

Deque rebalancing is called only with the destination half empty, as required by the helper's
algorithm. Priority scheduling checks equal-priority order. Roster reconciliation checks
duplicate arrivals and removal of both present and absent keys.

## Complete indexes and validation boundaries

All current API records and active syntax nodes have an application route. This is a
maintained reading map rather than exhaustive coverage of every combination of types,
borrows, effects and execution states. In particular, resource-exhaustion adapters have
real error branches, but examples do not exhaust host memory or threads to force them.
Native fault-injection and compiler regression tests supply that separate evidence.

See [known-limitations.md](known-limitations.md) for compiler/runtime issues exposed by the
applications. Reproduce the current indexes with:

```sh
python3 tools/check_example_coverage.py --check
python3 tools/check_example_syntax.py --frontend build-debug/r-front --check
ctest --test-dir build-debug -R 'r_example_' --output-on-failure
```
