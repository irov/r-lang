# Function overloading and standard-library method audit

Status: implemented. General function overloads, registered standard methods,
and direct await of method calls use the existing ownership and checked-error
contracts. The executable examples use the new spellings.

## Scope

This audit covers all 37 modules in the implementation inventory, including its
nine R-source modules. Its 977 records include types, operation families, and
concrete specializations; they are not 977 distinct functions. Receiver methods
in R-source modules were also inspected directly because they are not all
represented as individual operation records.

## Language rules

User functions, standard functions, and methods follow the same static selection
policy. Standard aliases bind to existing runtime operations; user overloads bind
to a concrete function or generic instantiation.

1. An overload belongs to one module name or one owner/member name. Distinct
   canonical value-parameter lists or arities distinguish overloads. Return type,
   checked errors, `async`, `unsafe`, visibility, and parameter names do not.
   Type aliases such as `bytes` and `array<u8>` cannot distinguish overloads.
2. Resolve from the argument types and arity. Use the existing literal types and
   permitted argument conversions. Prefer a unique exact parameter match;
   otherwise require a unique compatible candidate. Reject remaining ambiguity
   and list the candidates. Expected result types do not select overloads.
3. Generic candidates retain structural argument inference and definition-time
   checking. A generic body's selected declaration must be justified by its
   constraints and remain bound after instantiation. Closing a type must not
   reselect a different overload. Do not add specialization or a constraint
   preference order; competing candidates remain ambiguous.
4. Resolve without evaluating arguments or committing moves, borrows, or effects
   for rejected candidates. Then check and lower the selected call once in the
   existing evaluation order. Its own checked-error set applies; do not union the
   effects of every overload.
5. Keep existing receiver rules. `builder.append(text)` borrows a mutable builder;
   consuming methods on a named Move value require `(move value).method()`.
   Ordinary value arguments acquire no new implicit moves or automatic borrows.
   Constructors without a receiver remain qualified calls.
6. Methods belong to their owner. Do not interpret every visible free function as
   an extension method. Register standard built-in receivers explicitly, including
   canonical container aliases, and diagnose method collisions deterministically.
7. C exports and callback entry points keep explicit, unique external names.
   Internal overloaded R functions use deterministic ordering by canonical signature.
   Exported interfaces must identify complete overload sets and their members.

The exact-versus-compatible rule intentionally avoids a new numeric conversion
ranking system. Literals with an insufficiently specific type require an explicit
suffix or cast rather than selection from the destination type.

## Formatting target

Both qualified and receiver forms are supported:

```r
std.format::builder builder = std.format::with_capacity(64usize);
std.format::append(&builder, "Receipt for ");
std.format::append(&builder, customer);
std.format::append(&builder, '\n');

builder.append("Receipt for ");
builder.append(customer);
builder.append('\n');
builder.append(total);          // A floating-point value.
builder.append(quantity, 10u32); // An integer with an explicit radix.

str view = builder.as_str();
// Finish after the last use of the borrowed view.
std.string::string text = (move builder).finish();
```

`append_str`, `append_char`, and numeric `append_*` become overloads of `append`.
Integer overloads retain the explicit radix parameter. Text and character appends
retain `std.alloc::alloc_error`; numeric appends retain
`std.format::format_error`. Existing transactional updates, UTF-8 guarantees,
allocation behavior, and runtime symbols remain unchanged.

An owning string is not implicitly moved by a formatting operation. Keep the
existing explicit borrowed-view conversion where needed; receiver convenience
must not silently introduce a conversion for other arguments.

## Module-by-module migration

The entries below describe API families, not a mechanical suffix-removal rule.
Methods expose existing operations with the same access and consumption contracts.
Qualified calls remain available where they are useful for explicit API examples.

| Module | Implemented treatment | Meaning that must remain explicit |
| --- | --- | --- |
| `core` | Unify typed checked, saturating, and wrapping arithmetic within each family. Keep reflection, raw memory, and atomic operations as intrinsics. | Checked, saturating, and wrapping arithmetic differ; preserve atomic memory orders, raw-pointer contracts, and mutable/shared access distinctions. |
| `std.alloc` | Keep allocation factories and explicit extraction operations. | `try_new`, byte allocation, and `into_value` have different ownership and failure contracts. |
| `std.arc` | Expose owner and weak-owner operations as registered methods; `clone` can select strong or weak receiver. | Raw conversion, weakening, upgrading, and consuming unwrapping remain distinct. |
| `std.rc` | Apply the same call style as `std.arc` with its existing capabilities. | Do not change thread-safety properties or combine raw and safe operations. |
| `std.array` | Add methods for capacity, reserve, clear, access, push, pop, remove, and views. Keep constructors qualified. | Preserve `get`/`get_mut`, shared/mutable slices, and returned-owner errors. |
| `std.list` | Add list and iterator methods for existing access and mutation operations. | Front/back, before/after, mutable access, and stable-borrow rules stay distinct. |
| `std.dict` | Add dictionary and iterator methods for lookup, mutation, reservation, and iteration. | Preserve key hooks, access qualifiers, and ownership returned by failed insertion. |
| `std.error` | Keep explicit conversion names; expose diagnostic/name queries on the common error value. | Error erasure remains an explicit conversion, never an overload-driven implicit conversion. |
| `std.bytes` | Add receiver operations where appropriate; distinguish scalar and slice append/find by their input types. Coordinate methods with `array<u8>`. | Keep endian names and widths in binary serialization. Preserve source/destination roles in copying. |
| `std.hash` | Keep named free functions for the five algorithms over byte views. | CRC32, MD5, SHA-1, SHA-256, and SHA-512 are different algorithms, not overloads of one operation. |
| `std.utf8` | Keep explicit validation and validity queries over byte views. | A Boolean query and a checked validated view are different results and contracts. |
| `std.bits` | Add reader methods. `read` needs an explicit receiver mapping because the existing reader argument is second. | Preserve bit width, alignment, input borrowing, and error offsets. |
| `std.secret` | Add buffer queries and views; retain clearly named zeroization and constant-time comparison. | Constant-time comparison must not become ordinary equality; keep explicit mutable access. |
| `std.string` | Unify UTF-8 string-view append and scalar append as `append`; add string queries, reserve, clear, and conversion methods. | Keep `append_utf8` for validation of arbitrary bytes. Borrowing, copying, and consuming conversions remain distinct. |
| `std.convert` | Keep destination-specific `parse_*` and `checked_*` names in this change. | Their input lists do not determine the destination type. Do not invent result-directed overload resolution. |
| `std.format` | Unify typed appends and expose builder methods, including consuming `finish`. Preserve existing template `.format(...)`. | Explicit integer radix, selected overload effects, borrowing, and transactional append remain unchanged. |
| `std.json` | Add tree, number, decoder, reader, and detached-result methods where a receiver exists. Keep encode/decode factories qualified. | Existing contextual decoding remains its own language contract. Keep `take` versus borrowed getters and `marshal` versus `stringify`. |
| `std.math` | Unify numeric type suffixes by argument type, including real/complex families where the operation name agrees. Keep pure operations as free functions. | Constants have no argument type to dispatch on. Preserve different operations such as magnitude and phase; mixed scalar types must not cause new promotions. |
| `std.time` | Expose duration and clock-value queries/arithmetic as methods; unify free arithmetic names only where argument types distinguish them. | Monotonic and system clocks, seconds and nanoseconds, and UTC conversion remain explicit. |
| `std.env` | Keep free functions for process environment state and argument enumeration. | There is no natural receiver; do not manufacture an environment object solely for method syntax. |
| `std.thread` | Add methods to existing thread, join-handle, and panic values where applicable. | Scoped/unscoped spawn, consuming join/detach, and park/unpark retain their different lifecycle contracts. |
| `std.sync` | Add methods for locks, guards, channels, once values, and barriers; remove redundant owner prefixes where receiver identity supplies them. | Blocking/try operations, read/write access, poisoning, bounded/unbounded behavior, and ownership recovery stay explicit. |
| `std.async` | Expose explicit consuming task methods for cancel and detach. | Preserve completion acknowledgement, cancellation cleanup, and the existing `await move name` form. |
| `std.io` | Add input/output methods; overload free `close` by handle type where useful. Keep standard-stream factories qualified. | Preserve `write`/`write_all`, shared-owner writes, deadlines, and explicit consuming close. |
| `std.fs` | Add path, file, directory, and iterator methods; remove redundant owner prefixes from member names. | Keep beneath-directory confinement, metadata distinctions, atomic publication, and no-replace behavior explicit. |
| `std.net` | Add listener, stream, and datagram-socket methods such as `accept`, `read`, `write_all`, and `local_address`. Keep connect/listen/bind factories qualified. | TCP/UDP creation, shutdown direction, addressed datagrams, and partial/all writes stay distinct. |
| `std.process` | Add command configuration and child-process methods. Keep process-global operations free. | Preserve spawn versus wait, consuming pipe extraction, termination, and process exit behavior. |
| `std.c` | Add string/handle queries and explicit consuming handle release where appropriate. | Keep destination-specific checked conversions, raw ABI signatures, callback symbols, attachment, and adoption/release contracts explicit. |
| `std.cmp` | Keep existing generic comparison functions and trait contracts. | `Equal` and `Ordered` must not be inferred from overload availability. |
| `std.iter` | Preserve trait iteration and generic adapters. Keep range constructors explicit; their names distinguish the construction contract. | Iterator adapters cannot become inherent methods of every foreign iterator without an explicit trait design. Keep array/list collection destinations explicit. |
| `std.set` | Retain its existing owner methods and update examples consistently. | Preserve key constraints and insertion ownership. |
| `std.deque` | Retain its existing owner methods and update examples consistently. | Front/back operations and potentially failing rebalancing remain explicit. |
| `std.slice` | Keep generic slice algorithms as free functions. | A slice is not a nominal owner under current method rules; do not add arbitrary extension-method lookup. |
| `std.heap` | Retain its existing owner methods and qualified construction. | Preserve ordering and Copy constraints. |
| `std.sorted` | Retain existing set/map methods and qualified constructors. | Preserve ordering constraints and set/map operation differences. |
| `std.text` | Keep text-view algorithms as free functions. | ASCII-only comparison and conversion must remain visibly ASCII-specific. |
| `std.regex` | Add methods to compiled regex values for matching, search, replacement, and splitting. Keep compilation and literal escaping qualified. | Preserve full match versus search, search offsets, replacement semantics, and compilation options. |

`await file.read(...)` and `await (move handle).close()` reuse direct-await task
materialization, two-phase startup, cancellation, and cleanup. A failed startup
preserves named Move arguments and receivers. Completion errors are observed at
`await`. Existing `await move task` remains available for a task started earlier.

The explicit registry is `library/standard_methods.json`, with generated compiler
bindings checked by `tools/generate_standard_methods.py --check`. It covers 242
members on 66 canonical owner spellings and 85 qualified overload families. The
implementation inventory exports each family with its original candidate contracts,
including the selected checked-error set. Method aliases do not add runtime ABI
entries. Interface version 9 adds canonical overload identities to function and
generic function-schema records. Canonical identities normalize generic parameter
names and distinguish complete parameter signatures.

Old qualified, suffixed standard operation names remain accepted for source
compatibility. Endian, validation, allocation, and destination-specific names
remain explicit. The examples and their generators use the new call style;
coverage is attributed to resolved HIR operations, so an alias still exercises
the original inventory contract.

## Verification coverage

- Overload contract tests cover ordinary and generic functions, methods and trait
  implementations, canonical aliases, result-only differences, ambiguous
  conversions, callable entry designators, access qualifiers, nested standard
  calls, and definition-time checking.
- Module-order checks compare generated C17 and interface v9, including complete
  ordinary and generic overload families.
- Runtime fixtures exercise Copy and Move results, selected checked errors,
  generic trait dispatch, text and character appends, explicit consumption, and
  direct await. An injected close startup failure verifies that the original
  handle is preserved and a retry closes it once.
- Application tests cover receipt formatting, calculator and numeric operations,
  JSON, collections, concurrency, filesystem/network examples, ZIP and unzip.
- The standard-method catalogue, source and syntax coverage, specifications,
  inventories, ABI/layout, C style, and formatting have dedicated checks.

Validation commands:

```sh
cmake --build build-debug -j8
ctest --test-dir build-debug --output-on-failure -j3
cmake --build build-debug --target specification-check library-coverage-check
python3 tools/check_example_coverage.py --check --require-complete
python3 tools/check_example_syntax.py --frontend build-debug/r-front --check --require-complete
```
