# R example applications

These programs show language features in small applications with command-line input and
observable results. The catalogue contains 58 applications, including ZIP/unzip, and nine
focused walkthroughs. Applications cover all 1548 current public API inventory records and all
102 active parser node kinds. These indexes identify where to read a feature; command tests
verify runtime behavior. Compiler regression fixtures remain separate from applications.

Build the compiler and run the application checks from the repository root:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R 'example_' --output-on-failure
```

Each new application is built by `r_frontend_codegen_example_NAME` into
`build-debug/tests/codegen_example_NAME`. Its README lists commands and expected behavior.
The same build harness compiles each application with `r-front --emit=object`, checks its stack
bounds and runs it in the sanitizer configurations.

| Application | Purpose | Main language/library topics |
|---|---|---|
| [Tournament](tournament/README.md) | Schedule matches, rounds and league tables | Comprehensions, ranges, membership, traits, associated types and constants, derived implementations, owning closures, variadic spread, exchange, cloning and slice rotations |
| [Background jobs](jobs/README.md) | Deduplicate and supervise checksumming work | Derived generic key hooks, drop hooks, cancellation, detachment, finally and payload lifetime |
| [Preflight](preflight/README.md) | Validate service configuration | Common error domains, typed error adapters, boundary checks and resource diagnostics |
| [C bridge](c_bridge/README.md) | Exchange strings and owned packets with C | Header-verified imports, raw callbacks, handles, views anchored to a handle or to bytes (`core::slice_from_raw_parts_in`), attachment and UTF-8 views |
| [Logbook](logbook/README.md) | Filter events and inspect log schemas | Enum reflection, public fields, explicit discriminants and owning payload variants |
| [Calculator](calculator/README.md) | Real and complex scientific calculations, arithmetic and postfix expressions | Every concrete `std.math` operation, C numeric types, public records, checked errors, a recursive descent parser bounded by `@recursion(depth = 16)`, a state machine on a labeled switch |
| [Numbers](numbers/README.md) | Radix conversion and integer arithmetic | All integer specializations, checked conversion, overflow, saturation and wrapping, bit counts, rotations and wide arithmetic |
| [Binary](binary/README.md) | Checksums, telemetry packets, device labels and bit operations | Byte views, hashes, little-endian encoding written field by field (`fields(Trait)`), bit reading, fixed arrays, 128-bit arithmetic on limbs |
| [Text](text/README.md) | Search, redact, split and inspect text | Complete regex API, UTF-8 boundaries, strings and reusable storage |
| [Deflate](deflate/README.md) | Compress and decompress DEFLATE, zlib and gzip streams | Resumable `std.deflate` coders over caller buffers, flush modes, one-shot helpers, checked stream errors |
| [XML](xml/README.md) | Stream, select and rewrite XML | Streaming `std.xml` reader over fragments, namespaces, path selectors, escaping writer |
| [Statistics](statistics/README.md) | Measurement summaries, paginated reports, runs and search | Iterators, callables, associated items, slices, arrays and lists, containers of views, scoped tasks taking views |
| [Playlist](playlist/README.md) | Edit an ordered list of track IDs | Complete list API, stable node borrows, consuming removal, switch fallthrough |
| [Keystore](keystore/README.md) | Store values in a backend chosen by configuration | Traits behind a `dyn` interface, a session owning its backend as `own dyn(Store)*`, fixed and allocating backends, checked capacity errors |
| [Pipeline](pipeline/README.md) | Bound, scale and summarize measurements | Generic callables with checked error sets, opaque stage results, function items, exact error unions |
| [Word statistics](wordstats/README.md) | Count words given on the command line | Containers of structs holding borrowed strings, of exclusive and of shared borrows, exclusive borrow results, optional borrows, `never` functions |
| [Tally](tally/README.md) | Summarize numbers as labelled report columns | Tuples, type packs, recursion over a pack ended by `len(T...)`, spreads of a tuple into a call |
| [Settings](settings/README.md) | Check settings given on the command line | A family of errors with a common base, a catch of the base, a rethrow that keeps the exact error, the standard error root `std.error::fault` |
| [Warehouse](warehouse/README.md) | Maintain stock quantities by SKU | Complete dictionary API, checked updates, iteration and replacement values |
| [Snapshots](snapshots/README.md) | Publish shared versions and refresh subscriptions | Complete rc/arc APIs, weak references, optional owners, raw tokens |
| [Buffer comparison](buffer_compare/README.md) | Compare and erase separately owned buffers | Complete secret-buffer API, checked allocation, adoption and extraction |
| [Receipt](receipt/README.md) | Print captured receipt templates | Named captures, repeated positional slots, formatting and builder reuse |
| [Clock](clock/README.md) | Convert calendars, date texts and durations, tick an interval, and show wall-clock times in time zones | Complete time API, RFC 3339 and HTTP dates, `std.time::interval`, native public records, direct await and monotonic deadlines, zones from TZif files and POSIX TZ rules with local times, gaps and folds |
| [ZIP](zip/README.md) / [Unzip](unzip/README.md) | Create and extract archives | Modules, binary formats, checked errors, files, async I/O and cleanup |
| [Workspace](workspace/README.md) | Manage journal records and inspect files | Directory capabilities, file metadata, cursor I/O and atomic no-replace publication |
| [JSON workbench](json_tool/README.md) | Normalize configurations and edit JSON | Every JSON field parameter, generic schemas, hooks, exact numbers, incremental input and reader handoff |
| [Register workbench](registers/README.md) | Update control words and issue concurrent tickets | All atomic operations, CAS, unsigned wrapping and volatile register simulation |
| [Synchronized ledger](ledger/README.md) | Post transactions and acknowledge a handoff | Mutexes, barriers and condition variables |
| [Cached price quotes](quotes/README.md) | Calculate tax and publish price snapshots | Read/write locks, once validation and lazy immutable policy |
| [Worker pipelines](workers/README.md) | Check queued messages and sum data in parallel | Native threads, bounded channels, scoped borrows, joining and notifications |
| [Network laboratory](netlab/README.md) | Deliver local messages, inspect addresses, tune sockets, dial by name | Complete networking API, TCP tasks, UDP truncation, resolver and deadlines, socket options and multicast, connection by name |
| [Local services](ipc/README.md) | Answer local clients over a Unix-domain socket, collect datagrams, raise a signal | Unix-domain listeners, streams and datagrams, peer credentials, `std.stream` over a Unix-domain stream, a signal `select` that stops the service, `std.signal::raise` |
| [TCP service](service/README.md) | Serve echo, counter, watched and budgeted requests on a loopback port; stop on SIGTERM with a drain; read its command line and layered settings and log what it does | `std.service` with a bounded handler group, connection timeouts, a handler inside a `budget` block with `std.alloc::limits`, an auditor reading a channel with an asynchronous `for`, state under `std.async::mutex` and `rw_lock`, bounded reports through `reserve`, a `broadcast` watcher that lags, `std.signal` listeners raced in a `select`, `std.args` options and help, `std.config` defaults, JSON file, environment and options, `std.log` text and JSON records to standard error or a file |
| [Offload](offload/README.md) | Run blocking C calls while other tasks keep running | `std.async::blocking` on the blocking call pool, a libc import, pool exhaustion and cancellation of a running call, `std.async::task_id` of a pool call, `std.async::join` of a call that panicked |
| [Testing](testing/README.md) | Test a version module with @test functions run as a program | `@test` in test mode, `std.test` assertions, expected errors, an asynchronous test and allocation-failure runs |
| [Status](status/README.md) | Print device records, readings and endpoints as text | `core::Format`: `@derive(format)`, an own implementation, a generic bound, widths, network addresses and `dyn(core::Format)` |
| [HTTP service](http/README.md) | Serve a JSON catalogue over HTTP and HTTPS and request it with the client | `std.http` router with path parameters, before and after hooks, `serve` and `serve_tls`, the client with a connection pool and redirects, `std.json` bodies, `std.url` |
| [Realtime chat](realtime/README.md) | Run a WebSocket chat room and find it through SRV, TXT and PTR records | `std.websocket` connect/accept, receive and send from two tasks, `std.http` upgrade routes, `std.dns` resolver and wire form, `std.async::broadcast` |
| [Assistant memory over MCP](assistant/README.md) | Keep notes for an assistant as an MCP server over stdio and HTTP, and hold a conversation with it from the client of the same program | `std.mcp` server with tools, resources, a template, a prompt and completion, tool schemas from `std.json::schema::<T>()` and `@json(description)`, a confirmation through a multi round-trip request, progress, `subscriptions/listen` changes, tools that run as tasks of the Tasks extension (`run_as_task`, `run_tasks`, `start_tool`, `get_task`, `answer_task`, `cancel_task`, `finish_task`), `serve_stdio`, `route` over `std.http`, the client over HTTP and a launched child process |
| [Notary](notary/README.md) | Sign, verify, seal and open short messages as COSE messages with keys derived from a passphrase, show CBOR items in diagnostic notation, sign with ES256 and RSA keys kept in PEM, and encrypt with AES-CBC | `std.cose` Sign1, Mac0 and Encrypt0 with header buckets, `std.crypto` Ed25519, X25519, ChaCha20-Poly1305 and XChaCha20-Poly1305, HKDF, Argon2id password hashes and BLAKE2b, ECDSA on P-256, RSA with PKCS#1 v1.5 and PSS, AES-CBC, PKCS#8 and SPKI keys in PEM, `std.cbor` value trees, the streaming encoder, deterministic decoding and diagnostic notation, SHA-384 and HMAC-SHA-384 from `std.hash` |
| [Device registry](registry/README.md) | Keep devices in SQLite with a transactional outbox: register, rename and retire devices, list pending events and deliver them in order | `std.sqlite` connections in write-ahead-log mode, prepared statements run again, values of every storage class and typed reads of rows, `BEGIN IMMEDIATE`/`EXCLUSIVE` transactions with commit and rollback, `busy` from a writer that does not wait, read-only connections, operations that return started tasks of the blocking call pool |
| [Telemetry](telemetry/README.md) | Serve one HTTP application on TCP, TLS and a Unix-domain socket at once with metrics, spans, health, an idle timeout and a drain on SIGTERM | `std.service::serve_all` with TCP, Unix and TLS listeners, `std.service::connection` and `std.service::health`, `std.http::serve_all` with the `/health` and `/metrics` endpoints of a router, `std.metrics` counters, gauges and histograms in the text format of Prometheus, `std.trace` spans with parents and attributes as JSON lines |
| [Ingest](ingest/README.md) | Parse batches of `key=value` messages in worker tasks, each with an arena lent by a pool and inside a budget whose usage it reports | `std.arena` blocks, pieces and views read back as text, `std.pool` leases of a fixed set of arenas, budget blocks (Core R-STMT-0020) with `std.alloc::budget_usage` and the refusal `budget_exhausted` |
| [Journal](journal/README.md) | Keep fixed-size records in a data file with a write-ahead journal that several processes use in turn, and repair an update interrupted between the journal and the data write | `std.fs::lock`, `try_lock` and `unlock` on whole files and record ranges, `read_at`, `write_all_at`, `read_at_into` and `write_all_at_from` at explicit offsets, `std.fs::sync` with the `barrier`, `device` and `media` levels, records read and replayed through `std.fs::map_file` mappings |
| [Orders](orders/README.md) | Keep products and orders in PostgreSQL: orders take stock in transactions, every order is announced to listeners, and data moves as CSV | `std.postgres` over TCP, TLS and a Unix-domain socket with SCRAM-SHA-256, md5 or cleartext passwords, parameters, prepared statements, transactions, `LISTEN`/`NOTIFY`, `COPY`, cancel and a pool of connections |
| [Arena](arena/README.md) | Issue and check the session tokens of a mobile game, get access tokens for Google APIs, read the settings of the server from its environment, keep its users in PostgreSQL and serve its HTTP API | `std.jwt` ES256 and HS256 tokens with claims as structs, JWK Sets, `std.oauth2` service-account assertions (RFC 7523), client credentials, authorization codes with PKCE and a shared token cache, `std.config::from_environment` and `decode` into structs, `std.postgres` migrations, rows read into structs, INSERT and UPDATE statements made from structs, timestamps, uuids, arrays and JSON as typed parameters and reads, and a `std.http::app` with a context per request, middleware by prefix (sessions, AES-CBC bodies, a transaction per command with kept answers), CORS, cookies and a handler whose panic is answered with 500, a log of one JSON record per request in a chosen layout with bound fields, and the reports of unobserved panics |
| [TLS](tls/README.md) | Echo over TLS 1.3 and check certificates on a loopback connection | `std.tls` client and server sessions over TCP streams, ALPN, trusted authorities from a file or from the system bundle and `SSL_CERT_FILE`, a private key in `std.secret::buffer`, name, authority and expiry rejections |
| [Byte pipe](bytepipe/README.md) | Copy bounded stdin data and validate text | Binary I/O, shared writes, partial progress and consuming UTF-8 conversion |
| [Byte streams](streams/README.md) | Relay stdin through a file, TCP and UDP | Scoped I/O operations that loan caller buffers to a task group until the backend acknowledges each outcome |
| [Relay](relay/README.md) | Number input lines, read record files and talk line protocols over TCP and a pipe | `std.bufio` readers and writers over `std.stream` implementations: standard input and output, files, TCP connections, `own dyn(std.stream::Stream)*` and a `duplex` of child pipes; numbered lines through a `core::AsyncIterator` and an asynchronous `for`; `std.console` |
| [Textkit](textkit/README.md) | Split and trim fields, number lines, fold case, encode and decode bytes, pack integers into records, summarise numbers and show regex groups | `std.text` pieces, lines, scalars and owned results; `std.encoding`; `std.bytes::cursor` and `std.string` edits; `std.iter` filter, reversed, chunks, windows, min, max and sum; `I::Item:` header constraints; `std.regex` captures |
| [Tokens](tokens/README.md) | Make and parse identifiers, sign and verify messages, digest standard input and draw reproducible numbers | `std.uuid` versions 4 and 7, `std.hash` HMAC and digests in pieces, `std.random` operating-system bytes, uniform draws and the seeded generator |
| [Dispatch](dispatch/README.md) | Plan deliveries and reconcile bookings | Deque, heap, set, sorted containers, custom ordering, comparison helpers and a command table of function values |
| [Environment](environment/README.md) | Inspect process-local configuration | Environment snapshots, arguments, optional owned values and error classification |
| [Process status](process_status/README.md) | Exercise supervisor exit handling | Orderly process exit and explicit abnormal termination |
| [Runner](runner/README.md) | Run commands and capture output | Processes, environment policies and concurrent stdout/stderr capture |

Focused examples also live in [generics](generics/README.md), [methods](methods/README.md),
[reflection](reflection/README.md), [tables](tables/README.md), [format](format/README.md),
[JSON](json/README.md), [regex](regex/README.md), [pointers](pointers/README.md) and
[C callbacks](c_callbacks/README.md).
Some of these are short executable walkthroughs rather than command-line utilities.

## Exit statuses

Applications report their own outcomes with statuses 0…111: 64 for usage errors, 65 for
invalid data and the other values listed in each README. `main` does not catch standard
environment failures such as allocation, task start, threads, clocks or standard-stream I/O.
Such an error reaches the implicit `main` error boundary (Core R-FUNC-0008), which prints its
domain, name and code on stderr and exits with 112 (allocation), 113 (I/O, filesystem),
114 (network), 115 (conversion, format), 116 (async runtime, threading) or 117 (other
standard domains). User errors, `std.json::error` and the owning `push_error`, `insert_error`
and `new_error` families still need a catch in `main`. Every application prints through
`std.console` (Library R-SLIB-CONSOLE-0001), so a failed write to standard output also reaches
that boundary with 113.

## Coverage and maintenance

[catalogue.json](catalogue.json) identifies application sources and their runtime checks.
[catalogue.cmake](catalogue.cmake) registers the multi-module applications with CTest.
[coverage.json](coverage.json) is generated from the standard-library implementation inventory
and operation references plus resolved HIR types in application source, excluding comments,
string literals, imported implementation bodies and test fixtures.
A closed numeric API family counts as covered only when all of its concrete spellings occur.

```sh
python3 tools/generate_calculator.py --check
python3 tools/generate_numeric_example.py --check
python3 tools/check_example_coverage.py --write
python3 tools/check_example_syntax.py --frontend build-debug/r-front --write
python3 tools/check_example_coverage.py --check --require-complete
python3 tools/check_example_syntax.py --frontend build-debug/r-front --check --require-complete
```

CTest requires both indexes to remain complete: a new public API or active syntax node needs
an application route. Walkthrough-only coverage is reported separately. Operation spellings and
resolved HIR types are an index, not
proof of execution. Command tests independently check output and exit status, including error
paths and empty input. [syntax-coverage.json](syntax-coverage.json) separately indexes active parser nodes in application
sources. [LANGUAGE.md](LANGUAGE.md) maps language topics and source-defined methods to programs.
Neither a parser node nor a method name proves all of its semantic cases; the runtime command
checks remain the execution evidence.

New examples should have a useful purpose, a short README, input validation and observable
results. Add runtime checks for the documented commands. Do not copy assertions from a compiler
fixture into a directory and count that as an application, or insert unused API calls to improve
a coverage number. Keep commands, README text and source comments in English.

[Fanout](fanout/README.md) calibrates sensor readings through an opaque callable and computes
a summary using two supervised tasks borrowing the same owned input; a `select` with a deadline
clause consumes whichever finishes first.
