# Compiler implementation audit

## Current state (12 September 2026)

The sections below this one record the audit history in the order it was performed;
several of their intermediate counts are superseded. Re-running every tool on the
current Debug `r-front` gives:

| Check | Result | Command |
| --- | --- | --- |
| Acceptance matrix | 75 of 75 probes accepted (M13 added enumeration reflection, tagged-union reflection and type/field reflection inside a generic; M12 added an aggregate with borrow fields, a generic iterator adapter over a closure and an associated function of a generic owner; M11 added an array literal, a comprehension, dict expressions and a variadic function; M10 added a range-for, a core iterator, an associated type and a membership test; M9 added a local lambda, a moved capture, a callable constraint and an auto local; M8 added struct methods, method calls, associated functions, trait implementations, trait bounds and a scalar implementation; M6 added open ranges, module constant expressions, small-integer negation, fixed-array owner moves, explicit panic, never paths, char and repr(C) enum casts, char comparisons) | `tools/audit_compiler_implementation.py` |
| Public source surface | 939 of 939 operations resolve (879 C operations, 15 static reflection intrinsics of R-LIB-0024 and 45 functions and methods of the standard modules written in R, probed through `import` with `library/r/library.map`); 0 without lowering; 20 of 20 limit constants and 182 type spellings (161 C types, 21 R-source traits and aggregates) reach C17; unknown-type control rejected | `tools/audit_library_source_surface.py` |
| Explicit `R-DIAG-SLICE-001` sites | 23 call sites, 20 unique messages after L13 (27 and 24 after the M6 classification, 71 and 61 before it); since Core draft.65 specification section 25.1 lists no valid form that the compiler rejects, so each remaining site is a guard whose reach by a valid program is a defect | `tools/audit_semantic_slices.py` |
| Lowering rejections (L14.2) | 369 `R_FRONTEND_NOT_LOWERABLE` branches in 109 functions of MIR, C17 and the C ABI bridge (async frame shapes 207, standard operation shapes 57, HIR tree shapes 42, program structure 24, bridge 18, types 14, JSON schemas 3, MIR 4); none is reached by the type-position sweeps, the fixtures, the L14.3 generator programs, the 689 replayed codegen tests or the nesting-limit programs; each is a guard whose reach by a program that semantic analysis accepted is a defect | `tools/audit_lowering_rejections.py` |
| Safety probes | 101 of 101 | `tools/audit_language_safety.py` |
| Benchmark pairs | 5 R/C pairs in `tests/bench` agree on their checksums; `docs/benchmarks.md` records the -O2 timings after the static stack discipline (R-to-R call R/C 0.99 with no per-call check, down from 1.48 and +0.5 ns per call under the per-call preflight; fixed and slice indexing 1.00 and 0.99; checked i64 arithmetic 1.04 on Apple M4) | `ctest -R r_bench_pairs_agree`, `cmake --build build-release --target benchmarks` (Release configuration; the harness refuses Debug runtime archives) |
| Static reflection (M13) | `core::enum_count/min/max/variants`, `variant_count`, `field_count`, `field_name`, `type_name` fold to HIR literals in `compiler/semantic/reflection.inc` (call-free, generic parameters folded by the instantiation clone), `core::target_name`/`profile_name` fold to program strings from the generated target identity table, and `enum_name`/`enum_ordinal`/`enum_at`/`enum_from_name`/`variant_name` lower in both emitter paths to one translation-unit-local static helper per selection and enumeration (a `switch`, or a static name table searched by the shared lookup helper), called from every call site; no runtime or library symbol, freestanding-capable; executable walk-through in `examples/reflection`; Core 0.1.0-draft.41 (R-REFL-0001..0004, 460 rules, Annex A 254 productions), Library 0.1.0-draft.12 (R-LIB-0024, 213 rules) | `ctest --test-dir build-debug -R 'reflection\|freestanding_program'`, `tools/audit_compiler_implementation.py` |
| Standard modules written in R (M12) | `library/r/library.map` (`MODULE = PATH [PROFILE]`) and `--library-map`: a reachable `import std.name;` loads the listed R source as a library source with its least profile (`R-DIAG-PROFILE-001` below it), its items resolve like those of any imported module and reach C17 only through the importing program; `std.cmp` (`ordering`, `Equal`/`Ordered`, scalar implementations, `min`/`max`/`clamp`), `std.slice`, `std.text` in `freestanding`, `std.iter` (sources, `map`/`filter_map`/`take`/`skip`/`enumerate`/`zip`/`chain`, consumers, `collect_array`/`collect_list`), `std.set`, `std.deque`, `std.heap`, `std.sorted` in `allocation`; compiler prerequisites: aggregates with borrow fields in signatures with conservative origin propagation, qualified trait constraints, owner-spelling inference for associated functions, callable-signature unification, borrow parameters of lambdas and callable constraints, `len` over `list`/`dict`; inventory records `implementation_language: "r"`/`r_source` (36 modules, 946 items, 66 R-source items); Core 0.1.0-draft.40, Library 0.1.0-draft.11 (210 rules) | `ctest --test-dir build-debug -R 'library\|borrow_field\|iterator_adapter\|generic_set\|qualified_constraint\|callable_signature'`, `cmake --build build-debug --target library-layout-check library-coverage-check verify_library_inventory` |
| Collection expressions and variadic parameters (M11) | `[...]`/`{k: v}` literals and comprehensions with nested `for`/`if` clauses lowered to a hidden container plus `std.array::push`/`std.dict::insert` under the ordinary effect rules (the comprehension body callback appends to the loop body, so nested clauses add no C block levels), `T... name` parameters as `const T[]` with a hidden caller-side pack `[T; n]` dropped after the call, `...operand` spreads of slices, arrays and fixed arrays, result-borrow and spread/pack diagnostics; interface schema 8 (`variadic=true`) | `ctest --test-dir build-debug -R 'collections\|variadic\|comprehension\|spread'` |
| Lambdas and closures (M9) | local `fn` declarations lowered to an environment aggregate plus a `call` method, implicit borrow captures (exclusive on write, address-of or method call) and explicit `move(...)` value captures with the borrow rules applied to the environment, direct closure calls, callable constraints `@generic<F: fn(P...) -> R>` as compiler-made traits keyed by the interned signature, `auto` locals; generated C17 keeps direct calls only | `ctest --test-dir build-debug -R 'closure\|lambda\|auto_storage'` |
| Methods and traits (M8) | methods with an explicit `this` receiver in three forms, associated functions, `Type::name` and `receiver.name(...)`/`receiver->name(...)` call syntax, one flat method namespace per type, `trait`/`impl` with static dispatch for nominal, standard and built-in targets, trait names in the generic constraint vocabulary, `R-DIAG-TRAIT-001` for incomplete, mismatched, orphan and duplicate implementations, interface schema 6 with `(trait ...)`/`(impl ...)` records; the generated C17 contains direct calls only, so the acyclic call graph of R-FUNC-0004 stays exact | `ctest --test-dir build-debug -R 'method\|trait'`, `ctest --test-dir build-debug -L normative` |
| Freestanding profile (M7) | `runtime/freestanding` core runtime (`-ffreestanding`; environment panic handler, adopted stack bounds, thread-local hook), `targets/arm64-apple-darwin.freestanding.json` validated against the hosted twin (shared C ABI sections, closed panic/stack/allocator/threads/floating contracts), profile-bound manifest digests in the emitter, freestanding generated C without hosted entry or C guards, `own`/`core::adopt` rejected without an allocator; `r_runtime_freestanding` unit and `r_frontend_freestanding_program` (compile with `-ffreestanding`, `nm -u` allowlist, hosted environment stub, bounds and stack-exhaustion panics observed by the environment) | `ctest --test-dir build-debug -L freestanding` |
| Slice classification (M6) | every former slice notice is implemented, reclassified as a normative diagnostic, kept as an unreachable invariant, or recorded as a limitation: implemented `panic(message)` with `explicit` panics in sync and async frames, `never` expression statements, open ranges, module constant folding with `R-DIAG-CONST-001` overflow, small-integer unary operators, `char` comparisons, `char`/integer and fieldless `@repr(C)` enum/integer `as` conversions with `invalid_conversion` guards (`codegen_{panic,async_panic,scalar_casts,async_scalar_casts,char_compare,char_cast_failure,enum_cast_failure,async_enum_cast_failure,open_ranges}`); normative rejections for unknown type names, `void` fields, aggregate attributes, non-function calls, non-numeric casts, borrow `await` destinations, view drops and try/catch states (`semantic_{never_statement,panic_message_type,cast_operands}` plus the earlier probes); Core revision 0.1.0-draft.37 | `ctest --test-dir build-debug -L normative`, `python3 tools/audit_semantic_slices.py` |
| Static stack discipline (M5b) | acyclic R call graph with one `r_runtime_stack_require(R_STACK_ENTRY(<entry>))` check per entry and no per-call preflight; self-nesting graphs through `own`/`rc`/`arc`, `o<...>`, fixed arrays and `array`/`list`/`dict` values are destroyed by `r_runtime_drop_iterative` with generated cursors (one-million-node chains, 500 000-node trees and 250 000-deep container nests pass in `r_runtime_iterative_drop` and `codegen_stack_recursive_{own,rc,arc,enum,value,array,list,dict,fixed,option,owner_array}`); recursive calls, self-nesting through dict keys or standard types, and derived JSON of self-nesting types are rejected as `R-DIAG-STACK-001` | `ctest --test-dir build-debug -R 'stack\|iterative_drop'` |
| Deny-panic-alloc policy over the library surface | 939 of 939 operations, 20 constants and 182 type spellings resolve under `--deny-panic-alloc`; no closed operation is rejected | `tools/audit_library_source_surface.py --deny-panic-alloc` |
| C style and formatting gates | 998 handwritten C files valid | `c-style-check`, `format-check` |
| Full CTest, Debug | 1521 of 1521 including the M13 reflection cases (codegen `reflection`, `async_reflection`, `reflection_example`, the freestanding program, five normative fixtures, parser syntax and recovery tests), the M12 library-layer cases (codegen `library_cmp`, `library_iter`, `library_containers`, `library_slice_heap`, `library_sorted_text`, `borrow_fields`, `iterator_adapter`, `container_len`, `generic_set`, seven normative fixtures, the inventory tooling test over 36 modules), the M11 collection and variadic cases (codegen `collections`, `variadic`, nine normative fixtures, parser syntax and recovery tests), the M10 iteration cases (codegen `range_for`, `membership`, `associated_types`, eight normative fixtures, parser syntax and recovery tests), the M9 closure cases (codegen `closures`, six normative fixtures, parser syntax and recovery tests), the M8 method and trait cases (codegen `methods`, `traits`, `generic_methods`, `async_methods`, `method_example`, ten normative fixtures, parser syntax and recovery tests), the R-FFI-0044 record digest cases, the M7 freestanding cases (runtime unit, program under an environment stub, profile negatives), the M6 slice-classification cases (explicit panic, scalar casts, char comparisons, open ranges, normative negatives), the static stack discipline (recursion negatives, `codegen_stack_entries`, the iterative-drop runtime and codegen cases), the profile gates, the `extern "C"` import slice, the ABI verifier, the bridge, opaque types, verified constants, imported objects, variadic imports, ABI-record-proven complete aggregates, managed-token adapters, the R-CONF-G009 matrix, the `std.secret` unit, sync/async codegen and capability tests, the moved-local effect-exit regression, the deny-panic-alloc policy cases and the benchmark pair agreement | `ctest --test-dir build-debug` |
| Sanitizer subset (ASan, UBSan) | 288 of 288 FFI, verifier, ABI-record, method/trait, closure, range-for/membership/associated-type, collection/variadic, library-layer (R-source modules, borrow-field aggregates, qualified constraints, callable signatures), static reflection (sync, async and freestanding) and normative (including the R-FFI-0044 stale-header, wrong-compiler and interface-fingerprint cases), profile (including the freestanding `own`/`core::adopt` negatives), link-manifest, interface, `std.secret`, deny-panic-alloc, stack-discipline and iterative-drop tests | `ctest --test-dir build-sanitize -R 'ffi\|abi_verifier\|profile\|link_manifest\|interface\|secret\|deny_panic_alloc\|stack\|iterative_drop\|runtime_containers\|method\|trait\|normative\|closure\|lambda\|auto_storage\|range_for\|membership\|associated\|for_in\|projection\|collections\|variadic\|comprehension\|spread\|library\|borrow_field\|iterator_adapter\|container_len\|generic_set\|qualified_constraint\|callable_signature\|reflection\|freestanding'` |

Library profile selection is now enforced through `R-DIAG-PROFILE-001`, and
`extern "C" { ... }` blocks import C functions with scalar, raw-pointer and raw
function signatures: ordinary identifiers through the direct declaration path,
reserved-class identifiers through the generated bridge translation unit, both
proven against the declared headers by the generated ABI verifier and resolved
against the link manifest; see `compiler/README.md`. The same blocks now declare
`opaque struct` types with `@c_type` tag, union or typedef spellings, verified
`@c_constant` integer constants, imported (`const`, `thread_local`) C objects,
variadic prototypes and complete `@repr(C)` structs and enums proven one-for-one
against an ABI record produced by `tools/generate_c_abi_record.py` (R-FFI-0017,
R-FFI-0040, R-FFI-0041), each also probed by the verifier unit; a record is also
checked against its block's provider, the target manifest and C17 options.

`std.secret` (library specification 0.1.0-draft.10, section 7.5, R-SLIB-SECRET-0001..0004)
is the 28th library module: `std.secret::buffer` is a Move-only, Send, non-Sync owner whose
drop glue erases the whole allocation through volatile stores before release, with
`with_length`, `from_bytes`, `len`, `as_slice`, `as_slice_mut`, `zeroize` and
`constant_time_equal` lowered on both the synchronous and the async frame paths and
recorded in the closed runtime entry-stack inventory (revision 15). Adding it exposed and
fixed a latent emitter defect: effect-exit cleanup blocks were never preflighted, so a
Move local dropped only on a checked-error path before being moved into a later call
(`bytes` into `std.string::from_bytes` or `std.secret::from_bytes`) had no declared drop
glue; `codegen_moved_local_effect_exit` now guards that path. Fixed-array slices
(`u8[N]` used as `u8[]`) remain not lowerable inside async functions, for `std.bytes::fill`
as much as for `std.secret::zeroize`.

The deny-panic-allocation policy of Core R-OBJ-0012 (Core revision 0.1.0-draft.33) is
selected by `@deny_panic_alloc` before the `module` declaration or by `--deny-panic-alloc`
for the whole translation. It rejects `new`, `new arc` and `new rc` with `R-DIAG-ALLOC-001`
and any standard operation whose inventory record carries `panics_on_allocation_failure`;
the generated operation registry carries that flag per entry and Library R-LIB-0007 keeps
it false everywhere, which the policy run of the surface audit confirms. A misplaced or
duplicate attribute is `R-DIAG-SYN-002`; `std.alloc::try_new` and the checked
`alloc_error` operations remain the recoverable path (`codegen_deny_panic_alloc`).
Managed-token adapters, C-origin ingress validation and the R-CONF-G009 matrix
(`docs/ffi-conformance-g009.md`) close the M2 gate. The known remaining
implementation gap at this date is the `integrated-partial` hosted-profile
components recorded in the target manifest. ABI records now carry the compiler
identity and header digests into the build: the frontend proves the identity
against the target manifest toolchain and every header digest against the header
roots given with `--abi-header-dir`, and both digests enter the interface and
link-plan fingerprints (R-FFI-0044, `r_frontend_abi_record_{ffi_record_stale_header,
ffi_record_wrong_compiler,interface_fingerprint}`).

This bounded audit checks whether selected specified R programs reach C17 output.
It complements library C unit tests and inventory checks. A library operation can
have a complete C implementation while remaining unavailable to R source.

The historical baseline recorded on September 10, 2026 contained **25 acceptance
probes: 4 accepted and 21 failed**. Twenty failures were source diagnostics; the
fieldless enum fallthrough probe exited with status 2 without a diagnostic. Those
were 21 failing examples, not a count of every incomplete language feature.

The original matrix remains green: the current implementation accepts all **25
of 25 historical probes**. The first extension added 11 specified programs, all
of which remain accepted. The storage-lifetime extension adds five more positive
programs: the current result is **36 accepted and 5 rejected out of 41**. Its
Debug `r-front` SHA-256 is
`1b2e213d853b7673dc425ecba4962f20951a5b719e519e9c955b1565000da7f5`.
The first table below preserves the historical failures and the implementation
work that closed them. The former extended gaps and their resolution are recorded
after the inventory validation boundary. Acceptance remains a frontend and code-generation
check; executable regressions cover the implemented runtime paths separately.

## Repeating the audit

After building the compiler, run from the repository root:

```sh
python3 tools/audit_compiler_implementation.py \
    --compiler build-debug/r-front > build-debug/compiler-implementation-audit.json
```

The [audit tool](../tools/audit_compiler_implementation.py) exits with status 1 if
any specified example fails. Every probe expects successful compilation; rejection
is never accepted as a passing negative test. JSON output contains the exact
source, normative rule, compiler identity, exit status and diagnostics. Temporary
sources are removed automatically. A 30-second bound applies to each invocation.

The tool asks `r-front` for C17 in the `hosted-native-async` profile. It does not
compile or link generated C, execute a program, contact a network, or open a child
process described by the R sources. Passing is frontend/code-generation acceptance
only; each implementation still needs runtime, ownership and cleanup tests.

## Completed callback scope

The compiler now lowers user-defined exported `extern "C"` functions, including
`@callback` definitions, with supported C scalar and raw-pointer signatures.
`@safety` and optional `@export_name` are validated. Non-null and nullable
`raw fn(...) -> ...` values have concrete semantic types and C17 function-pointer
representations; storage, parameters, returns, nested function-pointer types and
indirect calls are implemented. Indirect calls require `unsafe`; calling a null
function pointer fails a runtime contract check. Ordinary R function designators
do not become first-class function values.
Executable coverage is in
[audit_std_c_raw_fn.r](../tests/fixtures/audit_std_c_raw_fn.r).

`std.c::adopt_handle` now accepts a raw data pointer and a destructor with the
exact C signature. The Move owner calls its destructor once on destruction;
`release_handle` transfers the pointer without invoking the destructor. Checked
error cleanup and async use are exercised by
[audit_std_c_adopt_handle.r](../tests/fixtures/audit_std_c_adopt_handle.r) and
[audit_async_std_c_adopt_handle.r](../tests/fixtures/audit_async_std_c_adopt_handle.r).

Exported entry wrappers create a temporary thread attachment when needed and
preserve an existing explicit attachment across nested callbacks. Internal entry
guards check nesting order and reject explicit detachment with a live entry.
Outbound indirect calls and handle destructors save the host floating environment,
establish `FE_DFL_ENV`, and restore the saved environment. Inbound exported entries
perform the corresponding foreign-environment isolation. These internal guards
do not change the public `std.c` attachment representation. The
[runtime boundary tests](../tests/runtime_c_boundary_tests.c) cover nested calls,
explicit attachments, fresh pthreads, contract checks and Darwin ARM64 FPCR/FPSR
normalization and restoration, including flush-to-zero control.

## Remaining FFI foundations

This implementation does not complete the general verified `extern "C"` import,
header, ABI-record and link-bridge pipeline for arbitrary external C libraries.
Existing manifest, header or C-library tooling is not evidence that every imported
source declaration reaches executable code. That integration still requires
verified symbol/type resolution, exact header-compatible bridges and the
corresponding generated calls under R-FFI-0016..0022 and R-CMAP-0018..0022.

`@repr(C)` structs and enums, declared in an `extern "C"` block or in R, pass by
value, through pointers and inside raw function types of imports and callbacks
(R-FFI-0004, R-FFI-0017, R-FFI-0021): a C-declared aggregate is proven against its
`@c_type` inventory, an R-declared struct against the C struct its ABI record places
at each position, and every value that enters R from C is validated through its
`@repr(C)` members (R-FFI-0056; stage L13.2 of
[language-completeness-roadmap.ru.md](language-completeness-roadmap.ru.md)).

These are remaining implementation work, not proposed language exclusions. Full
FFI conformance and full source-level coverage of every library operation are not
claimed by the callback implementation or this audit.

## Historical baseline gaps

All language rules below refer to the normative
[Core specification](../specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc).
Library rules refer to the
[Standard Library specification](../specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc).
The implementation checks are in
[semantic.c](../compiler/semantic/semantic.c); diagnostic excerpts identify the
relevant paths even as source line numbers change.

| Feature and acceptance probes | Normative contract | Historical result | Implemented work |
| --- | --- | --- | --- |
| Multidimensional fixed arrays: `multidimensional_array` | R-TYPE-0011 specifies `T[N][M]`, with the leftmost bound outermost. | `multidimensional fixed array is valid R but outside the bounded value-type slice` | Preserve every dimension in type construction, layout, initialization, indexing and cleanup. |
| Signed indexing and range bounds: `signed_index`, `signed_range` | R-EXPR-0021 requires a checked conversion to `usize`. | `checked index conversion is valid R but outside semantic slice 3`; `checked range-bound conversion is outside semantic slice 3` | Lower integer-to-index conversion before the existing bounds checks, with the specified bounds failure behavior. |
| Compound arithmetic: `mixed_compound`, `small_compound_shift` | R-EXPR-0013 uses arithmetic promotions and an intrinsic conversion back to the destination. | Mixed `u64 += u32` and `u8 <<= usize` are rejected as outside their semantic slices. | Apply the same promoted arithmetic as ordinary expressions, then signed checked or unsigned modulo conversion at commit. |
| Character switch: `char_switch` | R-STMT-0006 explicitly includes `char` scrutinees and requires a default clause. | `char or enum switch is valid R but outside the bounded semantic slice` | Lower character case constants and dispatch. The diagnostic does not imply that ordinary enum switches are universally unsupported. |
| Thread-local objects: `local_thread_storage`, `module_thread_storage` | R-OBJ-0008 specifies per-thread initialization and destruction; storage declarations allow both scopes. | Block declaration reports `thread-local initialization is outside this lowering`; module declaration reports an unsupported module binding. | Implement per-thread storage, initialization state, teardown order and runtime attachment integration. Plain C `_Thread_local` emission alone does not satisfy the lifetime contract. |
| Mutable module object: `mutable_module_object` | R-OBJ-0001 and R-OBJ-0010 give a module object static storage duration and constant initialization. The probe does not access the mutable object. | `module binding is valid R but outside semantic slice 3` | Lower mutable module storage, textual-order initialization and reverse-order destruction. Preserve the separate R-OBJ-0009 access checks at each use. |
| Atomic values: `local_atomic`, `atomic_aggregate_local`, `atomic_parameter` | R-INIT-0012 requires initialization from the exact non-atomic base value. R-INIT-0013 defines exclusive moves of atomic values and containing aggregates. | A direct local and a struct containing `au32` report `local type is valid R but outside semantic slice 2`; a by-value parameter reports `function signature is valid R but outside semantic slice 2`. | Admit atomic values in locals and signatures, distinguish initialization from copy, prove exclusive move points and emit representation-safe relaxed atomic snapshots rather than ordinary C reads. |
| C-compatible aggregates: `repr_c_struct`, `repr_c_enum` | R-OBJ-0004/R-FFI-0006 define `@repr(C)` struct layout; R-AGG-0005 defines a fieldless `@repr(C)` enum with a C integer representation. | Both declarations report `aggregate attribute or modifier is valid R but outside the aggregate slice`. | Validate the closed attribute form, build target ABI layout and enum representation, and carry the representation through interface metadata and C17 emission. This does not by itself complete external header verification. |
| Dynamic-array slicing: `owned_array_range` | R-EXPR-0021 includes `array<T>[lo..hi]` and ties the slice to its owner. | `owned-array range is valid R but outside the bounded semantic slice` | Construct a slice over the owner's buffer and preserve structural-mutation restrictions and borrow provenance. |
| Multiple borrow origins: `conditional_borrow_origins`, `return_borrow_origins` | R-BORROW-0008 requires the output origin set from every reachable return path. | `multi-origin conditional borrow is valid R but outside semantic slice 3`; `multi-origin borrowed output is valid R but outside semantic slice 3` | Represent and merge origin sets, propagate them through calls, and export their interface mapping. Do not choose one origin or relax lifetime checks. |
| Fieldless enum fallthrough: `enum_fallthrough` | R-STMT-0007 permits transfer to a case that binds no payload; the probe has no clause locals. | Compiler exit 2 with no source diagnostic, including during HIR emission. | Diagnose any unsupported path and complete fieldless enum fallthrough lowering and cleanup. Add a successful runtime regression. |
| `std.net::tcp_local_address`: `network_address` | R-SLIB-NET-0006 specifies the exact borrowed input and checked result. | `called function name is unresolved at this source position` | Register the operation and carry its concrete result and checked effects through HIR/MIR/C17. |
| `std.process::clear_environment`: `process_builder` | R-SLIB-PROC-0002 specifies mutation of an existing command builder. | `called function name is unresolved at this source position` | Connect the existing C operation to its source-level signature, mutable borrow and generated call. |
| `std.fs::file_metadata`: `filesystem_metadata` | R-SLIB-FS-0006 specifies the async checked operation. | Direct `await` reaches call resolution, which rejects the operation name. | Register the native task signature, start/completion effects, result ABI and sync/async consumers. |

The last three were independently confirmed examples of the same library/compiler
boundary problem. Their C implementations are
[tcp_local_address.c](../library/std/net/source/tcp_local_address.c),
[clear_environment.c](../library/std/process/source/clear_environment.c) and
[file_metadata.c](../library/std/fs/source/file_metadata.c). They now have
source-level signatures and C17 lowering. This audit does not claim that every
other operation in those modules is complete.

Executable regressions cover multidimensional indexing, signed indexing and
ranges, promoted compound assignment, character switch and enum fallthrough,
atomic values, `@repr(C)`, owned-array ranges, multi-origin borrows, the three
library calls, and static/thread-local scalar storage. Module storage is also
tested through imports and async functions. C17 output for module objects is
stable when source modules are loaded in a different order.

Thread-local and mutable module objects currently support Copy integer values
with constant initialization. Move values, aggregate constant initialization,
per-thread destruction and reverse module teardown still require the complete
R-OBJ-0008/R-OBJ-0010 lifetime implementation. Passing the two bounded scalar
acceptance probes does not establish those broader lifetime contracts.

## Inventory validation boundary

[check_library_layout.py](../tools/check_library_layout.py) validates source files,
C symbols, headers, module CMake membership and inventory structure. For a source
record it checks that the declared C implementation exists. A missing
`conformance_status` currently defaults to `complete`; completeness rejects only
explicit `unimplemented` kinds and `partial` statuses.

That checker does not invoke `r-front`, verify resolution of a public R name, or
validate the generated program. Consequently, a zero-incomplete-record result
alone does not prove source-level integration. Scanning for `R_STANDARD_CALL_*`
in every pass has the same limitation: an operation absent from that enum never
enters the comparison.

The durable next step is a separate source-integration inventory that associates
each public operation or operation family with executable R coverage, including
required profiles, generic arguments, checked errors and sync/async entry points.
Do not infer conformance solely from the presence of a C implementation or a
source-token match. The audit tool provides an initial acceptance check, not that
complete inventory.

## Exhaustive public source-surface audit

The [source-surface audit](../tools/audit_library_source_surface.py) expands every
closed inventory family and checks all 872 concrete operation spellings, all 20
integer limit constants, and 160 concrete public type spellings. It also supplies
valid signatures for 13 names whose operation syntax collides with a type
constructor. The same result was reproduced with Debug and ASan/UBSan compilers:

- 775 operations are recognized and 97 are unresolved;
- five additional `std.sync` operations resolve by name but their valid
  signatures are rejected with `R-DIAG-SLICE-001`;
- all 20 `core::min_*` and `core::max_*` constants are unresolved;
- five parametric `std.sync` type spellings reach HIR but C17 generation exits
  with status 1 and no diagnostic;
- an invented `std.__source_surface_missing::type` follows the same silent C17
  failure, so the semantic type resolver does not reject an unknown standard
  type at its source location.

The 97 unresolved operations are grouped as follows: `core::validate_utf8` (1),
`std.arc` (10), `std.rc` (10), `std.fs` (19), `std.net` (17), `std.process`
(15), and `std.sync` (25). The JSON report contains every exact spelling,
inventory record, source signature, normative rule, generated probe and
diagnostic. A zero-argument probe only establishes name resolution; it does not
claim that the other 775 operations accept a valid signature. The 13 explicit
signature probes prevent constructor syntax from producing a false positive for
the known collisions.

The language grammar names six parametric `std.sync` types that have no type
facet in the generated inventory: `channel<T>`, `sync_channel<T>`, `sender<T>`,
`sync_sender<T>`, `receiver<T>` and `once_lock<T>`. The last one reaches C17 and
has an executable constructor regression. The other five are both absent from
the inventory's type facet and outside the frontend's value/signature slice.

Run the complete check with:

```sh
cmake --build build-debug --target library-source-surface-audit
```

The target intentionally remains red while any advertised source name is
unresolved, a valid collision signature is rejected, a type fails C17, or the
unknown-type negative control is accepted past name resolution.

## Exhaustive explicit-slice inventory

The [semantic-slice audit](../tools/audit_semantic_slices.py) statically records
every `R-DIAG-SLICE-001` emission site. The current semantic implementation has
64 call sites, 66 message alternatives and 57 unique messages. This closes the
previous review gap where only hand-selected messages were discussed. A static
entry is evidence of deliberately incomplete lowering, not by itself a
reproduced language defect; each entry still requires a program that is valid
under the current specification.

Five newly reproduced entries concern allocation-free empty `bytes` objects
with static storage. Module, immutable module and module `thread_local` objects
are rejected before C17. Block `static` and block `thread_local` forms are
instead rejected as non-constant initializers. R-TYPE-0030 defines `{}` for
`bytes` as allocation-free, and R-OBJ-0008/R-OBJ-0010 define the corresponding
storage lifetime, so all five remain positive acceptance failures in the
41-probe implementation audit.

## Exhaustive core integer regression

[generate_core_integer_fixture.py](../tools/generate_core_integer_fixture.py)
generates sync and async runtime coverage for all 90 checked, wrapping and
saturating add/subtract/multiply operations over the ten R integer types. Debug
execution passes at both `-O0` and `-O2`; ASan/UBSan execution also passes at
both levels. The optimized regression recompiles the same generated C17 against
the conservative measured `-O0` stack bounds. Its generated-C formatting
regression fails:
the `isize` checked-multiply helper and six long async assignments are not
clang-format clean. This is a C17 backend formatting defect, not a runtime-value
failure. It remains visible as
`r_frontend_codegen_core_integer_all_format`; this audit did not change the
backend to make the test pass.

The codegen harness now also recompiles the ownership-sensitive generic vector
OOM path, checked `finally`, JSON tree and async JSON tree fixtures at `-O2`.
All five optimized fixtures pass in Debug and ASan/UBSan. They retain the normal
C wrapper and use conservative measured `-O0` stack bounds, so the optimized
run preserves fault injection and runtime stack enforcement.

## C style gate regression

The standalone `c-style-check` target currently rejects four locations in
`compiler/semantic/semantic.c`. `r_semantic_collect_module_constant` jumps to an
`append_symbol` label, and `r_semantic_record_error_borrow_parameter` jumps to an
`add_parameter` label. The checker requires each handwritten `goto` to target the
function's final label and requires that label to be named `cleanup`. This is an
existing repository gate failure outside the audit-only changes; full CTest and
`format-check` do not execute this check.

## Hosted profile remains explicitly partial

The generated library inventory reports zero partial public records, while the
Darwin hosted-native-async target manifest still sets
`profile_conformance_claim` to false for four components: the type-erased task
executor, filesystem adapter lane, Dispatch I/O payload adapter and nonblocking
socket adapter. Their status strings all end in `integrated-partial`. The target
manifest checker deliberately preserves these declarations, so a green
`library-coverage-check` must not be interpreted as full hosted-profile
conformance. The source-surface failures for filesystem, network and sync APIs
make this boundary observable from R source.

## Async thread-local regression was scheduler-dependent

The TSan matrix exposed a flaky test in `codegen_async_module_storage.r`. Two
sequentially awaited tasks were assumed to run on the same executor worker, so
the second access was required to observe the first worker's `thread_local`
increment. The target manifest explicitly uses a private concurrent queue and
does not promise worker affinity. Under load the second task ran on another
worker and the generated program returned `-2` (process status 254), without a
ThreadSanitizer report. The fixture now accepts the two valid results: 15 on the
same worker and 13 on a fresh worker, while still checking the serialized shared
module update. The repaired test passed 20 consecutive TSan runs. The five
optimized fixtures also pass under TSan. This was a regression-test defect
rather than a compiler race.

## Extended acceptance gaps resolved on September 10, 2026

The first extended matrix contains 36 positive probes. Debug and ASan/UBSan builds now
accept every probe. The fixes cover constant Copy module storage, aggregate module
and thread-local storage, block-static aggregate parsing, compound borrow outputs,
multi-origin checked-error payloads, and all specified integer and atomic core
intrinsics.

The atomic integration includes compile-time and runtime ordering validation,
`atomic_compare_exchange_result<T>` payload switching, and overflow-checked signed
fetch add/sub through a C17 compare-exchange loop. Regression programs cover sync
and async execution, both compare-exchange outcomes, unsigned wrapping, signed
non-overflow, overflow panic, and generated-C formatting.

The safety audit now passes all 101 probes. In particular, ordinary assignment to
an initialized atomic object is rejected while move followed by full
reinitialization remains accepted. Inventory coverage reports 866 public records
with zero partial or unimplemented entries. The validation boundary described
above remains relevant: executable source tests, rather than inventory status
alone, establish that these paths reach and run generated C17.

## Exclusions and validation boundaries

- No independent GNU GCC is installed in this environment; `/usr/bin/gcc` is
  Apple Clang. Strict C17 coverage therefore uses Apple Clang only.
- Mixed-width ordinary comparison and bitwise operations passed. Old fallback
  messages for those operations remain in semantic source but did not establish
  a current gap. Positive controls also cover `usize` indexing and same-width
  compound assignment.
- Traits, specialization, const generics, explicit generic-function type
  arguments, generic C exports and first-class ordinary R functions are excluded
  by the current specification. Their absence is not counted as an implementation
  defect. Raw C function pointers have a separate specified contract.
- Rejected syntax such as implicit Boolean conditions, nested actual function
  calls in call-free operands, or borrow-bearing user async parameters is not
  counted as an implementation gap. The retained probes obey those restrictions.
- The historical baseline established compiler acceptance failures. It did not report a
  memory-safety vulnerability, inspect malformed-input behavior, or establish a
  total count of unfinished work. Runtime tests and the broader public API surface
  remain separate validation work. The separate
  [language safety audit](deep-language-safety-audit.ru.adoc) records sanitizer,
  malformed-input and ownership findings.
