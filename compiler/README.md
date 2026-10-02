# R Frontend Parser 0.1

This directory contains the bootstrap frontend for the normative English R Core
Specification `0.1.0-draft.95`.

```text
UTF-8 source
  -> re2c lexer
  -> tokens and trivia
  -> lossless CST
  -> aggregate-name classification
  -> compact AST
  -> whole-program semantic analysis
  -> typed HIR
  -> typed control-flow MIR
  -> strict ISO C17
```

The implementation is ISO C17. `r_frontend` is an internal static library and
`r-front` is a developer CLI; neither exposes a stable external ABI in 0.1.
The current implementation covers whole-program module, name, visibility, and
function-signature analysis; canonical scalar, atomic, aggregate, pointer, owner,
container, option, checked-error, and task types; deterministic typed HIR; typed basic-block
MIR; reproducible compiler-interface artifacts; and an executable C17 backend. The
synchronous backend supports the declarations, expressions, statements, checked
operations, aggregate initialization, ownership transfers, and Standard Library calls
used by the complete `examples/unzip` program.

HIR/MIR keep fixed-array and container operations, checked indexing and slicing,
typed throws, handler dispatch, rethrow, pending completion, LIFO `finally` routing,
Move places, async suspension, and cleanup state explicit. Automatic propagation is
resolved before C generation; each normalized checked-effect set has one deterministic
tagged carrier rather than nested source-level result values. Generated slices are
pointer/length pairs, every index and range is checked before access, and an invalid
access reports `R_RUNTIME_PANIC_BOUNDS`. Call-bounded borrow metadata covers the full
normative limit of 127 arguments.

The backend preserves left-to-right evaluation, implements checked signed arithmetic
without C undefined behavior, and uses the small hosted runtime under `runtime/`.
For the Darwin target the stack is bounded statically (R-FUNC-0004): the semantic pass rejects
any recursive call chain as `R-DIAG-STACK-001`, so every entry into R code has a worst-case
bound. An aggregate may reach itself through owned storage along `own`, `rc` and `arc`
owners, `o<...>`, fixed arrays, `array`/`list` elements, `dict` values and by-value struct or
enum members, nested in any order; such a group is destroyed by the runtime destroyer
`r_runtime_drop_iterative` with generated cursors (`r_type_cursor_a<id>` for aggregates,
`r_type_cursor_t<type id>` for the wrappers on the way, descriptors `r_type_node_*`, hook
wrappers `r_type_hook_a<id>`) that walk the object graph iteratively in the R-INIT-0010 order,
so the drop depth never follows the data. Self-nesting through a `dict` key, a standard type,
a `task` or an atomic is rejected as `R-DIAG-STACK-001`, and so is a derived JSON encoder or
decoder of a self-nesting aggregate (its generated glue would recurse with the data). Generated C
checks that bound once per entry through `r_runtime_stack_require(R_STACK_ENTRY(<entry>), ...)`:
the hosted `main` body, each spawned thread entry, each async step and helper gate, each
`call_once` initializer wrapper and each C callback wrapper. Calls between R functions and the
type-glue gates carry no check. A `raw fn` call is analysed as a call to every `@callback`
function whose exported signature matches the pointer type and marked in the generated C as
`/* R_STACK_INDIRECT: ... */`. Drop hooks join the graph only through storage the function
owns (declarations, by-value parameters, temporaries, moved values, replaced assignment
destinations); place expressions and borrows add no callee. Every participating
runtime thread still initializes immutable thread-local stack bounds before executing R code.

An async declaration `async T operation(...) throws E` is invoked as
`task<T throws E>`, with immediate checked effect `std.async::start_error`.
Generated functions reserve an owned stackless frame in runtime-managed heap storage
through the two-phase start protocol; the complete frame is never first materialized
as an automatic C object. A success-only initializer commits Copy and staged Move
arguments only after the last recoverable start check. `await move` consumes the named
task and exposes only its completion effects. `await operation(args)` first calls the
function into a hidden task slot, then consumes it using the same await lowering. The
call retains its immediate checked effects and transactional Move arguments.
Direct method calls, including `await (move handle).close(deadline)`, use the same
lowering and preserve the receiver on startup failure. Await composes in async expressions,
including nested arguments, return operands, conditions and loops. Static initializers,
synchronous functions and finally bodies cannot await. Await lowers to a non-blocking
resumable-task suspension edge, including projected task places. Only the
generated hosted C `main` blocks while observing the root task. Frame cleanup cancels
every still-owned task. Native async filesystem and I/O operations, owned values,
fixed-array and `std.array` slices, and the complete async `examples/unzip` entry path
are executable in the Darwin profile.

### Hosted main error boundary

Core draft.50 gives all four existing `i32 main` forms the closed Copy-only error set
in R-FUNC-0008. Explicit `throws` stays forbidden. User-defined errors, JSON errors and
owner-bearing allocation/container errors still require a local catch.
The generated C17 entry dispatches exact carrier tags, forms the portable error without
allocation and calls `std.error::name` through its C ABI. Emergency output includes domain,
name, code and native code. It precedes cancellation, a complete work drain, and static /
initial-thread TLS destruction; work created by each destructor drains before the next.
The executor remains available until destruction completes. Impossible tags are internal
contract violations, not another process status.

On `arm64-apple-darwin`, user main returns are `0..111`; allocation maps to 112,
I/O/filesystem to 113, network to 114, conversion/format to 115,
async-runtime/threading to 116 and all other portable domains to 117. Codes 118..123
remain reserved. An invalid user main result emits `invalid_main_status`, performs normal
cleanup and exits with 124. Startup statuses 125..127 are unchanged. Large or negative
main results are checked before native 8-bit status conversion.

### Conditional expressions are named before they leave

Core draft.51 forbids a conditional expression at any depth within a call argument or a return
operand (R-EXPR-0014): call, method, `format` and awaited-call argument lists, the `panic` operand,
standard type-call value operands and `return` in statements and switch clauses. The check runs
on the AST before name resolution (`semantic/conditional_positions.inc`) and reports one
`R-DIAG-FLOW-001` per outermost fork; initializers and assignments keep the operator.

### Explicit generic arguments

Core draft.52 adds explicit generic arguments (R-TYPE-0036), written `name::<arguments>` since
draft.53. The list follows a function name, a qualified name, `Owner<args>::name` or a receiver
method name and names every generic parameter the owner or receiver does not supply: types,
checked constants and `throws(...)` sets. The parser produces `generic_argument_list`;
`r_generic_close_explicit` closes the function through
`r_generic_apply_function` without inference, narrowing an overload family by the list and the
value-argument count. Result-only generic definitions are accepted. A closed name, and
`Owner::method`, in a value position is a function item; a method item takes its receiver as
the first parameter.

The compiler-recognized standard operations with type operands use the same spelling, so the
language has one style: `std.array::create::<T>()`, `std.array::with_capacity::<T>(n)`,
`std.list::create::<T>()`, `std.dict::create::<K, V>()`, `std.dict::with_capacity::<K, V>(n)`,
`std.sync::channel::<T>()`, `std.sync::sync_channel::<T>(n)`, `std.sync::once_lock::<T>()` and
the `core::` reflection forms such as `core::enum_count::<T>()` and `core::enum_from_name::<T>(s)`.
The parser still builds a `standard_type_call` node for them, so semantic lowering is unchanged;
the retired `op(T, ...)` spelling is a syntax error naming the replacement.

Core draft.53 writes every type and generic argument list between angle brackets:
`Box<i32>`, `array<u32>`, `dict<i32, bool>`, `o<Color>`, `task<i32 throws Bad>`,
`@generic<T: copy>` and trait applications `Read<T>`. Expressions use the explicit
`name::<arguments>`; `Cell<i32>::create(...)` and `Box<i32> { ... }` start with a type only
when the matching `>` is followed by `::` or `{` (`r_skip_balanced_angles`), otherwise `<`
is a comparison. `>>` closes two lists through `RParser.pending_greater`, and a constant
argument is parsed with `no_greater` so it stops at `>`; a constant containing `>` is
parenthesized. The AST drops the angle tokens of type, header, generic-argument,
standard-type-call and trait-name nodes, so semantic lowering is unchanged. Parameter
lists, `fn(...)`, `u8[N]`, `throws(...)`, `opaque(...)`, `sizeof(T)`, `new T(expr)` and
variant payloads keep parentheses. Diagnostics, reflection and instance names use the
same spelling (`grow<i32>`, `array<m::Level>`).

### Methods and traits

A method declares its receiver explicitly. `Ret Owner::name(const Owner* this, ...)`,
`Ret Owner::name(Owner* this, ...)` and `Ret Owner::name(Owner this, ...)` are the three
receiver forms (R-FUNC-0013); `this` and `Self` are keywords. A declaration without a
receiver is an associated function and is called through `Owner::name(...)`. `value.name(a)`
and `pointer->name(a)` call the method with the receiver as its first argument, taking the
borrow kind from the declared receiver form (R-FUNC-0014). Arguments may contain nested
calls and, in async functions, awaits. Their evaluation and temporary cleanup follow
source order. The method-call suffix remains the last suffix of its expression;
`a.b().c()` does not introduce method chaining. Calls such as `f(p.name())` and
`builder.append(text.as_str())` compose normally. Inherent and implemented methods share
one flat namespace per type, separate from fields and variants (R-NAME-0010).

`trait Name { prototypes };` declares prototypes over `Self`, and
`impl Name for Type { definitions };` binds them to one target type, which may be nominal,
standard or built-in (R-TYPE-0041, R-TYPE-0042). An implementation belongs to the module of
its trait or of its target, is unique per pair, and shall match each declared signature
exactly after substituting `Self`; deviations are `R-DIAG-TRAIT-001`. A trait name joins the
generic constraint vocabulary, so `@generic<T: Name & copy>` admits method calls on `T`
(R-TYPE-0043). Traits may have type parameters, constrained associated types, supertraits
and default method bodies. Definitions are checked even without an instantiation, and
inherited defaults preserve their checked errors and resource contracts. Closing a trait
application substitutes `Self`, its arguments and associated bindings, then selects one
coherent implementation. Generated C17 uses direct calls and keeps the static call graph
of R-FUNC-0004 exact. Dynamic dispatch exists only through dyn interfaces and function
values, below.

`opaque(contracts)` hides a function result's concrete implementation behind proven
capabilities, static traits or a callable signature. Associated types are fixed explicitly,
for example `opaque(core::Iterator & Item = i32)`. Callers use `auto` or generic inference;
closed code uses the concrete representation and normal cleanup, without boxing.

### Dyn interfaces

Core draft.59 adds `dyn(contracts)` (R-TYPE-0051), which exists only behind a borrow:
`dyn(Store)*`, `const dyn(Store & send)*` or their nullable forms. `semantic/dyn_interfaces.inc`
builds the closed type `R_SEMANTIC_TYPE_DYN`, whose base is a hidden parameter that carries the
contract (traits, capabilities, associated equalities). Equal contracts share one type: the
canonical spelling, traits and capabilities sorted and named by module, is interned in
`RGenericParameter.dyn_key` and doubles as the type's name. Contracts that name no trait, name a
callable, depend on generic parameters, leave an associated type open or contain a method that is
not dispatchable (receiver other than `const Self*`/`Self*`, own generic parameters, `Self`
outside the receiver) are rejected by `r_dyn_type` and `r_dyn_validate_contracts`.

A borrow of a type that proves the contract converts implicitly where an interface borrow is
expected (`r_dyn_convert` from `r_body_finish_value`); the conversion is an implicit `R_HIR_CAST`
that keeps the borrow origin, so exclusivity and lifetime are ordinary borrow rules. `&value` with
an interface context first forms a borrow of the value's own type (`r_dyn_borrow_context`). A
method call on an interface resolves like one on a constrained parameter, to the trait prototype
applied to the interface itself; that application symbol is the dispatcher.

After all bodies are built and closed, `r_dyn_resolve_targets` collects the members of every
interface (the closed referents of every conversion, including in instances) and binds each
dispatcher to the implementation for each member, instantiating default or generic methods;
it repeats with the closing loop until nothing changes. `r_dyn_assign_tags` numbers the members
of an interface in the order of their canonical type names, so generated code does not depend
on source order. The stack graph and `@noalloc`/`@nonblocking` proofs follow a dispatcher to
every target. C17 represents every interface borrow as `r_dyn { void *r_pointer; uint32_t
r_tag; }`; a synchronous dispatcher is a function with the prototype's signature whose body
switches on the tag and forwards every argument to the member's implementation, and an async
start of a dispatcher in an async body is expanded in place: one launch and call initializer
(`_m<tag>`) per member under a switch. Async methods take borrowed receivers only as
`@scoped async` methods inside task groups (R-BORROW-0024, R-STMT-0017). The
[keystore example](../examples/keystore/README.md) keeps the backend selected from its first
argument as `own dyn(Store)*` in a `Session` and runs every command through a borrow of it.

### Owners of interfaces

Core draft.81 adds owners of interfaces (R-TYPE-0055): `own dyn(C)*`, `arc dyn(C)`, `rc dyn(C)`,
`weak arc dyn(C)` and `weak rc dyn(C)`. The parser accepts `dyn(...)` after `own`, `arc`, `rc` and
`weak`; `r_dyn_owner_type` interns an `R_SEMANTIC_TYPE_OWN`, `ARC`, `RC` or `WEAK` type whose base
is the interface and rejects the nullable form, and `r_dyn_owned` recognizes such a type in the
later passes. `r_dyn_convert` adds three implicit `R_HIR_CAST` conversions: an owner of a closed
type to the same owner of an interface (the type proves the contract and is unborrowed), a borrow
or owner of an interface to one with a narrower contract (`r_dyn_accepts` applied to the wide
interface), and a borrow of an owner to an interface borrow of its member (a strong shared owner
lends only a const borrow, a weak owner nothing). `owner->method()` borrows the owner with the
receiver's access, casts that borrow to an interface borrow and dispatches like an interface call
(`traits.inc`), so a strong shared owner reaches only `const Self*` receivers. A dereference,
`try_unwrap`, `into_raw` and `get_mut` of an owner of an interface are rejected, since the member
has no nameable type. `r_dyn_resolve_targets` takes the members of an interface from owner
conversions too, and a narrowed interface receives every member of the wider one.

C17 represents an owner of an interface as the runtime owner of its member and the member's tag
(`r_own_dyn`, `r_arc_dyn`, `r_rc_dyn`, `r_weak_arc_dyn`, `r_weak_rc_dyn`, each
`{ <runtime owner> r_owner; uint32_t r_tag; }`). The member's type information is recorded in the
runtime owner when it is allocated, so release, clone, downgrade, upgrade and the counts reuse the
runtime operations on `r_owner`, and the last release destroys the member exactly once. A
conversion from a member owner adds the tag (`r_c17_emit_dyn_cast`); narrowing indexes a static
table `r_dyn_narrow_<wide>_<narrow>[]` with the wide tag (`r_c17_emit_dyn_narrowing_tables`); and a
borrow of an owner becomes the `r_dyn` of the member's storage and tag. Glue for a class of
owners is emitted once per class, with a representative that needs destruction. The keystore
example and the fixtures `codegen_dyn_owners`, `codegen_async_dyn_owners` and
`codegen_dyn_owners_modules` exercise locals, fields, containers, options, generic arguments,
narrowing, tasks, a thread and interfaces declared by another module.

### Scoped tasks and compositional payloads

`task_scope(N) group { ... }` supervises child starts in a bounded group. A reservation
precedes preparation; `std.async::start_error::scope_full` and preparation failure preserve
named Move arguments. `@scoped async` functions can borrow Send views of enclosing storage.
`await group.first(&a, &b)` selects readiness, and `await group.all()` is an all-members
barrier. Each result or checked completion error is observed separately with `await move`.
Scope exit cancels unconsumed members and waits for cleanup acknowledgement before releasing
borrowed storage, including when the parent is itself cancelled.

Core draft.60 composes these (R-STMT-0018). `await group.first_until(deadline, &a, &b)` returns
`o<usize>` and `await group.all_until(deadline)` returns `bool`; the deadline is a
`std.time::instant`, and `r_runtime_task_scope_wait_until` arms a `dispatch_after_f` wakeup for
the suspended waiter, so a passing deadline ends only the wait. `group.cancel_all()` lowers to
`R_HIR_TASK_SCOPE_CLOSE` with the `drop` operation and emits only the member discard of the
group exit: every unconsumed member is cancelled and consumed, and the next `all()` or the exit
observes acknowledgement. `select (group) { case T x = await move a: ... case until (d): ... }`
is parsed as `select_statement` and lowered by `semantic/task_scope_select.inc` into a group
wait (flags first, until and select) whose `usize` result is the clause index, followed by the
ordinary integer switch lowering with the last clause as its default. Select never cancels: a
listed member left unconsumed on some clause paths becomes `R_SEMANTIC_OBJECT_STATE_SELECT_PENDING`,
which merges with the consumed paths, cannot be named again, and is resolved by `cancel_all` or
the group exit.

`match(value) { case pattern if(condition): expression; default: expression; }` produces
a value and supports nested variants, named fields, guards and explicit Move bindings.
Coverage is checked independently of guards. The selected arm alone transfers payloads;
its temporaries survive suspension and retain exactly-once cleanup on cancellation. Native
opaque library outcomes retain their typed `switch` projections.

Borrow contracts retain separate origins for output fields and fixed indices, including
checked-error payloads and generic substitution. Complete component mappings distinguish
disjoint projections; incomplete information remains conservative. Neither metadata nor
opaque return types can extend the lifetime of borrowed storage.

Within a body, a borrow local also records the field path below its single origin that it
borrows (R-BORROW-0011): one byte per level, up to eight levels, an index keeping the path of
its array. An access to a disjoint field of the same object, directly or through another such
borrow, does not conflict with it; a merge at a branch or loop join keeps only the common
prefix, and a borrow whose target is not a direct field place covers its whole origin.

The [fanout application](../examples/fanout/README.md) demonstrates scoped shared input,
a select with a deadline clause and an opaque calibration closure. The
[dispatch application](../examples/dispatch/README.md) matches owned message payloads and
awaits delivery. The [painting example](../examples/methods/README.md) combines inherited
trait defaults and an opaque iterator.

### Asynchronous synchronization

Library draft.31 adds `std.async::mutex<T>`, `rw_lock<T>`, `semaphore`, `notify` and `broadcast<T>`
(R-SLIB-ASYNC-0013..0016) and slot reservations on bounded `std.sync` channels (R-LIB-0016).
`compiler/source/standard_async_sync.h` is one descriptor table: each operation names its C
symbol, its subject and result types, its rule and a shape that fixes the operands, the result
and the checked effects (a constructor, a synchronous call on a handle or guard, an access, a
consuming release, an asynchronous start or a `std.sync` outcome with a permit payload). `compiler/semantic/standard_async_sync.inc` checks the operands against
the descriptor (a shared or exclusive borrow of the handle or guard, a moved guard or permit, a
`usize` count, a value of the element type) and lowers a call to one `R_HIR_STANDARD_CALL`; a
start requires a Send, unborrowed result, and `get`/`get_mut` return a borrow bounded by the
borrow of the guard. `std.async::broadcast::<T>(capacity)` is a standard type call (Annex A)
whose element type comes from `::<T>` or from the expected type; the element shall be Send,
unborrowed and cloned structurally without a clone hook, so the library may clone it on any
thread. `compiler/codegen/standard_async_sync.inc` emits the synchronous calls and the task
starts, and passes the element's layout and clone glue to `r_std_async_broadcast`.

Guards and permits hold a reference to their lock, so they are unborrowed and may stay live
across `await`. The MIR await-liveness pass reports a `std.sync` guard, or a `std.sync` lock
outcome that carries one, as R-DIAG-ASYNC-001 [R-FUNC-0011] with the `std.async` lock that
replaces it. `std.async::broadcast_result<T>` is a compiler-generated outcome with the layout
`{ uint32_t r_tag; union { T r_received; uint64_t r_lagged; } r_payload; }` and its own move and
drop glue (`r_type_move_b*`, `r_type_drop_b*`); `std.sync::reserve_result<T>` and
`try_reserve_result<T>` reuse the tagged `std.sync` outcomes with a permit payload. The fixtures
`codegen_async_locks`, `codegen_async_broadcast`, `codegen_async_reserve` and the zero-capacity
panics `codegen_async_broadcast_zero` and `codegen_async_reserve_zero` run in every build, and
the service example uses every operation.

### Blocking calls

Library draft.32 adds `std.async::blocking(entry, arguments...)` (R-SLIB-ASYNC-0017), which runs
a blocking entry on the blocking call pool of Core R-TERM-0015. The compiler lowers it as it
lowers an unscoped `std.thread::spawn`: `r_body_lower_standard_thread_spawn` checks the direct
entry designator, the argument count and types, Send, the program region of borrows and the
direct `move place` form of staged Move arguments, and reports them under R-SLIB-ASYNC-0017. The
result is `task<R throws E...>` of the entry's return type and checked errors, which shall be
inhabited and hold no borrow (R-TYPE-0029), and the start carrier adds
`std.async::start_error`; the call is a transactional start, so a failed start leaves the staged
sources initialized. C17 reuses the thread entry supports (`r_thread_payload_*`,
`r_thread_stage_*`, `r_thread_entry_*`) and calls `r_std_async_blocking` with the payload type,
the type of the entry's completion storage, the entry trampoline and the stage, in the HIR path
of synchronous functions and the MIR path of async frames.

The pool (`runtime/darwin/source/blocking_pool.inc`, included by the task runtime) starts a
thread only while every existing one is busy, up to `R_RUNTIME_BLOCKING_THREAD_COUNT`, and takes
jobs from one FIFO. A start reserves a slot before its task commits, so submission never fails;
cancellation removes a job that no thread took, and a taken job acknowledges its task after the
entry returns, destroying the result when cancellation was selected first. `check_target_manifest.py`
compares the manifest's `blocking_call_pool` record with the runtime constants. The fixtures
`codegen_async_blocking` and `codegen_async_blocking_start` (with a wrapper that fails two
starts), `r_library_async_blocking_unit` and the offload example cover values, checked errors,
exhaustion, both cancellations and start errors.

### Formatting user types

Core draft.84 adds `core::Format` (R-TYPE-0046), declared with the other core traits in
`compiler/semantic/traits.inc` (`r_semantic_declare_core_format_trait`: no associated type, the
prototype `void format(const Self* this, std.format::builder* out) throws
std.alloc::alloc_error`). `r_semantic_type_has_standard_format` makes scalars, text, strings,
network addresses, `o<T>`, `T[N]`, slices, `array<T>` and tuples satisfy it structurally, so a
bound `T: core::Format` also admits them. Two internal standard operations carry the work:
`R_STANDARD_CALL_CORE_FORMAT_RENDER` renders a formatted-literal slot of such a type into a hidden
string that the slot then captures like any string (`r_format_lower_input` in
`compiler/semantic/format.inc`), and `R_STANDARD_CALL_CORE_FORMAT_APPEND` is `pointer->format(out)`,
`place.format(out)` (the `.format(` suffix of a place that is not a template, also at the end of a
chain) and the prototype call in bounded generic code. Both keep the formatted type in
`runtime_type`; `r_format_bind_type` records, for each nominal type that the closed type reaches,
the core::Format method of that type in `RSemanticAggregate.format_function`, at lowering and again
in every instantiation (`r_generic_clone_node`). The stack graph and resource effects visit those
methods through `r_format_visit_hooks`. A slot width now pads every value to a number of Unicode
scalar values (`r_library_internal_format_text` and `_char` in
`library/internal/text/source/format_spec.c`); the numeric specifications stay numeric.

C17 emits one glue function per formatted type (`compiler/codegen/format_glue.inc`,
`r_type_format_<id>(const void *, RStdFormatBuilder *)`), spliced after the function prototypes
like the clone glue: a nominal type calls its method through its effect carrier, standard
formatting composes the glue of the components with `some(...)`, `[..., ...]` and `(..., ...)`,
and addresses use `r_library_internal_net_ip_text`/`_socket_text` of std.net. RENDER creates a
builder and finishes it into the result string, destroying it on failure; APPEND writes to the
caller's builder; both transfer the allocation failure. An interface over core::Format accepts
members with standard formatting: the dyn binding records the prototype as their target
(`r_semantic_dyn_format_target`), the dispatcher calls their glue, and the dispatcher of the
synthetic prototype gets its HIR node at the first call (`r_dyn_synthetic_dispatcher`).
`@derive(format)` is the fifth capability of `compiler/parser/derive.inc`: it writes an
implementation that appends `Name { field: ` text and formats each field with `.format(out)`, and
`compiler/semantic/derive.inc` checks that each member satisfies core::Format. See
`tests/fixtures/codegen_core_format.r`, `codegen_async_core_format.r`,
`codegen_core_format_failures.r` with its allocation-failure wrapper,
`tests/l32_regression_tests.inc`, `tests/check_derive.py` and `examples/status`.

### Field initializers and default values

Core draft.85 lets a field declaration end with `= initializer` and an enum variant carry
`@default` (R-INIT-0004, R-INIT-0005). The parser keeps the initializer as an `initializer`
child of the field declaration and parses attributes before variants (the keyword `default`
names the attribute); the error-declaration and derive scanners skip those attributes. The
semantic field records the initializer's AST (`RSemanticField.initializer`) and
`r_semantic_check_field_initializers` checks each non-generic one once, in the module of its
aggregate, with checked effects allowed. `compiler/semantic/defaults.inc` separates the zero
default (`r_semantic_type_has_default`, one `default_value` node; a struct whose fields declare
initializers is no longer zero-default) from evaluated defaults
(`r_semantic_type_has_default_value`): an omitted field lowers its initializer where it is used,
with `RBodyContext.source` switched to the aggregate's module, `symbol_floor` hiding the locals
of the initializing function, `module_context` hiding lambda captures and `use_site` moving the
diagnostics of that use, such as an undeclared checked error, to the initialization. Structs of
defaults, `@default` variants and fixed arrays build ordinary `aggregate_init`, `variant` and
`array_init` nodes, so MIR and C17 need nothing new; omitted array elements and `core::take`
(lowered as a replacement with the evaluated default) use the same builder. An optional JSON
field without `default` maps a literal or `factory()` initializer to the existing JSON default
(`r_json_default_from_initializer`). Interface schema 31 writes `initializer=true` on such fields
and `default=true` on the default variant. See `tests/fixtures/codegen_field_defaults.r`,
`codegen_async_field_defaults.r`, `codegen_json_field_defaults.r`,
`codegen_field_defaults_failures.r` with its allocation-failure wrapper,
`tests/l33_regression_tests.inc`, `examples/settings` and `std.service::options`.

### Streams over standard and program types

`std.stream`, `std.bufio` and `std.console` (Library R-SLIB-STREAM-0001..0004,
R-SLIB-BUFIO-0001..0003, R-SLIB-CONSOLE-0001..0002) are R-source modules; Core draft.86 and the
M18 fixes made them expressible. An implementation of a trait for a standard or built-in type
may repeat a registered standard method name (R-NAME-0010): `r_generic_prepare_method` checks
the standard-method table only for inherent methods, the receiver form keeps selecting the
standard method before `r_body_resolve_method`, and a generic constraint or a dyn interface
reaches the implementation. `r_semantic_canonical_owner` keeps the standard type as the owner
of such methods even after a hidden field schema (for example of `std.net::tcp_connection`) has
given the type an aggregate (M18-9). An interface proves the capabilities its traits require of
`Self` (R-TYPE-0051): `r_dyn_type` adds them to the carrier before keying, so
`dyn(std.stream::Reader)` and `dyn(std.stream::Reader & send & sync)` are one interface and its
borrows cross `@scoped` calls. The portable error structs, `std.error::error` and
`std.bits::read_error` build by braced initialization
(`r_body_prepare_standard_error_constructor`, M18-2). The remaining defects found on the way:
`std.io::flush` through a borrowed stream is the two-operand MIR form that C17 passes as the
borrow itself (M18-3); a `@scoped async` method called as `value.method()` receives the borrow
that the call forms instead of a staged move (M18-4); a view formed by `owner.as_slice_mut()`
exposes the place it came from to the argument-conflict check, so views of different fields are
disjoint (M18-5); the task that an await in a loop condition materializes lives in the
condition's value scope (M18-6); the loop replay ignores stores of values without views
(M18-7); a call-formed view argument of a non-dependent slice parameter of a generic call is
lowered with that slice as its destination (M18-8); a move gate without a drop gate names a
keeper that names it back, so the generated C has no unused function (M18-1). See
`tests/fixtures/codegen_std_stream.r`, `codegen_std_bufio.r`, `codegen_std_console.r`,
`tests/m18_regression_tests.inc` and `examples/relay`.

### Associated type constraints and the R parts of C modules

A header entry `P::Name: constraints` constrains the associated type `Name` of the parameter
`P` of a generic function (Core R-TYPE-0043, M19), as in
`@generic<I: core::Iterator, I::Item: std.cmp::Ordered>`. The parser keeps it as a generic
parameter with an `ASSOCIATED_NAME` child. `r_generic_register_headers` registers such entries
after the parameters of the schema as holders of their constraints outside the schema
(`RGenericProjectionConstraint`), named after the associated type, and rejects them in the
headers of types, traits and implementations. `r_generic_resolve_projection_constraints` finds
the projection with `r_semantic_parameter_projection` (exactly one trait that `P` satisfies,
directly or through a supertrait, declares `Name`) and adds the capabilities and traits of the
holder to the projection parameter, so the body may compare, copy or add its values.
`r_generic_validate_projections` substitutes the projection with the arguments of every
instantiation, dependent ones included as for ordinary constraints, and validates the result
against the holder. Interface schema 31 writes the entries as
`associated_constraints=((associated="P"::"Name" constraints=(...)))`. Defect M19-1: the first
holders had no name, and `--emit=hir` of any program importing `std.iter` read the intern table
at index -1; the source-surface audit found it.

A library map entry under the path of a C module, such as `std.bytes = std/bytes.r
freestanding`, is the R part of that module (Library R-SLIB-RSRC-0001): `import std.bytes;`
translates it with the program, the C operations keep precedence and a registry miss resolves
to an item of the R part. `std.bytes::cursor` and `std.string::replace_range`/`insert_str` are
written this way, and the inventory records the part as `r_part` of the C module.
Defect M19-2: several modules of the library map named a profile below the C modules they
use (`std.string`, `std.bytes`), so they did not translate in it, and an import below the
profile of a module ended in an internal error while its items were collected.
`r_semantic_reject_library_profiles` now rejects such an import at the import before any
declaration is collected, the modules keep items that need a higher profile under a module
`@if` on `core::profile`, and `tests/check_library_profiles.py` translates every module of the
map in every profile. `std.convert::parse_error` is built by braced initialization like the error structs of
R-SLIB-ERR-0003 (`r_semantic_prepare_convert_parse_error_schema`), which the R-source
`std.encoding` uses to report non-canonical text. See `tests/check_projection_constraints.py`,
`tests/m19_regression_tests.inc`, the fixtures `codegen_library_text.r`,
`codegen_library_encoding.r`, `codegen_library_iter_reductions.r`,
`codegen_library_byte_cursor.r`, `codegen_regex_captures.r`, `codegen_async_library_text.r`,
`codegen_library_text_oom.r` (every allocation fails in turn) and `codegen_encoding_driver.r`
with `tests/encoding_differential.py`, and `examples/textkit`.

### Randomness, identifiers and message codes

`std.random::fill` (Library R-SLIB-RANDOM-0001, M20) is the one C operation of the new module
`std.random` (`library/std/random`, `r_std_random_fill` over `arc4random_buf`). The compiler
treats it as the byte-view call that follows the `std.secret` group:
`R_STANDARD_CALL_RANDOM_FILL` checks one mutable `u8[]` argument and a void result, lowers
through the `zeroize` path with `RStdRandomMutSlice`, sets `uses_std_random` for the
`r_std_random.h` include, and adds `r_std_random` to the link plan. Everything else of M20 is
R source: the R part of `std.random` (`next_u64`, `below` with the rejection limit
(2^64 − bound) mod bound, SplitMix64-seeded xoshiro256** `generator` with `shuffle`), the R
part of `std.hash` (`sha256_state`/`sha512_state` over data in pieces and `hmac_sha256`/
`hmac_sha512`, which erase their padded keys with `std.secret::zeroize`), and `std.uuid`
(RFC 9562 versions 4 and 7, `core::Format`, derived equal and ordered, `hash`/`equal` methods
for dictionary keys). A digest struct of `std.hash` has its byte field wherever its type is
named, not only after a digest operation was checked (`r_semantic_prepare_hash_schema`, defect
M20-1), and C17 copies the whole native array of a digest field with `memcpy` in synchronous and
asynchronous code. See `tests/library_random_tests.c`, the fixtures `codegen_library_random.r`,
`codegen_library_uuid.r`, `codegen_library_hmac.r`, `codegen_async_library_tokens.r`,
`codegen_hash_digest_values.r` and `codegen_hash_driver.r` with `tests/hash_differential.py`
(SHA, HMAC and the generator against Python), and `examples/tokens`.

### Time texts, intervals and standard records

M21 adds no standard operation: the R part of `std.time` (`library/r/std/time.r`, Library
R-SLIB-TIME-0006..0009) writes and reads RFC 3339 and HTTP dates and, under a module `@if` on
`hosted-native-async`, declares `std.time::interval`, whose `@scoped async` method `tick` sleeps
with `std.time::sleep_until` until the next point of its grid or completes the latest due tick at
once. Two defects were fixed on the way. M21-1: a braced constructor headed by any non-generic
type of the standard type registry (`std.time::system_time {...}`, `std.process::exit_status
{...}`, `std.fs::metadata {...}`, `std.c::target_info {...}`) is lowered like the typed
declaration of that type, which prepares its hidden schema, and `std.c::target_info` has that
schema for typed initialization as well. M21-2: `==` and `!=` over two values of one natively
implemented fieldless enum prepare the enum's hidden schema first
(`r_semantic_prepare_standard_enum`), so a parameter and the `code` field of a caught error
compare where no variant is named. See `tests/m21_regression_tests.inc`, the fixtures
`codegen_library_time_formats.r`, `codegen_library_time_interval.r`,
`codegen_standard_record_constructors.r` and `codegen_time_driver.r` with
`tests/time_differential.py` (a reference written from the rules and the Python standard
library), and `examples/clock`.

### Signals, socket options and Unix-domain sockets

M22 adds three families of standard operations. `std.signal` (Library R-SLIB-SIGNAL-0001..0003)
follows the process operations: `R_STANDARD_CALL_SIGNAL_LISTEN` and `_RAISE` are checked
synchronous calls with a hosted allocator argument, and `_NEXT` starts like
`std.process::wait` (`r_std_signal_next`, a `u64` completion) through the external task
protocol; `uses_std_signal` adds `r_std_signal.h` and the link plan adds `r_std_signal`. The
runtime (`runtime/darwin/source/signal.c`) keeps one `DISPATCH_SOURCE_TYPE_SIGNAL` source per
kind that listeners share, installs `SIG_IGN` only after the source is registered and restores
the previous disposition with the last listener; a wait is cancelled by its slot, because a
completion may win over an earlier cancellation. The socket options of `std.net`
(R-SLIB-NET-0012..0013) and `unix_peer_credentials` (R-SLIB-NET-0016) are descriptor-driven
synchronous calls on a borrowed handle (`r_c17_net_is_option`); `o<std.time::duration>` maps to
the library's canonical `RStdTimeDurationOption` like `o<std.time::system_time>`. The
Unix-domain operations (R-SLIB-NET-0014, 0015, 0017) are descriptor entries: the asynchronous
ones in `standard_net_operations.h`, whose emitter now passes any `str` first argument as a
string view and accepts `u32` operands, and the reads, writes and datagram transfers in
`standard_scoped_operations.h`. Their handles have their own C types but share the TCP
listener, TCP stream and UDP socket storages and paths in `library/internal/networking`.
Four defects were fixed. M22-1: the receiver form of an operation of a natively implemented
enum reached through a qualified constant (`std.signal::kind::user1.raise()`) stopped the
standard-method unwrap at a non-expression child. M22-2: a spawned child inherited the signal
mask of the Dispatch thread, so `posix_spawn` now resets the mask and the replaced
dispositions. M22-3: a view of the arguments of `main` that a `@scoped` call captures lives in
the frame, and the step no longer declares an unused local copy of it. M22-4: the cancel
handler of a signal source reads the registration record under the signal mutex, which orders
it after the registering thread for the thread sanitizer. See
`tests/m22_regression_tests.inc`, `runtime/darwin/tests/signal_tests.c`, the fixtures
`codegen_library_signal.r`, `codegen_library_net_connect.r`, `codegen_library_net_options.r`,
`codegen_library_net_unix.r` and `regression_async_main_scoped_startup_view.r`, and the
examples `service`, `netlab` and `ipc`.

### Logging, arguments and configuration

M23 adds three R-source modules and one standard operation. `std.log`, `std.args` and
`std.config` (Library R-SLIB-LOG-0001..0003, R-SLIB-ARGS-0001..0003, R-SLIB-CONFIG-0001..0003)
are ordinary R modules of `library/r/library.map` that the compiler translates with the
program; they need no compiler support beyond what they use. A logger renders each record on
the calling task and hands the line to a bounded `std.sync` queue with `try_send`, so logging
never waits; the writer drains the queue in its own task after dropping the sender it keeps
for making loggers. Field names are rewritten instead of rejected, so the logging calls throw
only standard errors. `std.async::task_id` (R-SLIB-ASYNC-0018) is the descriptor
`R_STANDARD_CALL_ASYNC_TASK_ID` of shape `R_ASYNC_SYNC_QUERY_U64` in
`standard_async_sync.h`: no operands, a `u64` result and no effects. It reads the running task
of the executor, or the task of the blocking call on a pool thread
(`r_runtime_task_current_id`); the executor numbers tasks from one when it commits their start.
The parser admits `task_id` after the keyword module `std.async`, and the resource checker
proves it `@noalloc` and `@nonblocking`. One defect was fixed. M23-1: `std.convert::checked_D`
and `std.c::checked_D` in an asynchronous function were rejected by the C17 lowering, because
the MIR path of the step had no emitter for them; `r_c17_preflight_async_checked_call` and
`r_c17_emit_async_checked_call` now store the kernel result as the single-error carrier of
`std.convert::range_error` that the MIR dispatches. See `tests/m23_regression_tests.inc`, the
fixtures `codegen_library_log.r`, `codegen_library_args.r`, `codegen_library_config.r`,
`codegen_library_async_task_id.r`, `codegen_library_task_id_outside.r` and
`regression_async_checked_conversion.r`, and the examples `service` and `offload`.

### Tests in R

M24 adds the attribute `@test` and test mode (Core R-FUNC-0025). The parser accepts `test`
among the attribute names, with the arguments `expect = Type` and `allocations`, and
`r_semantic_external_modifiers` records them; `r_semantic_check_test_function` requires a
module-scope, non-generic function without parameters whose result is `void`, accepts
`expect = E` only when `r_semantic_error_catch_distance` finds a declared error that a catch of
E receives, and accepts `allocations` only on a synchronous function. `r-front --test` calls
`r_frontend_set_test_mode` before any source is added; the entry module is the module of
`--entry`, or else the first source. `compiler/parser/test_entry.inc` then works like the derive
expansion: `r_test_scan_imports` adds the imports of `std.test` and `std.console` to the entry
module, and `r_test_expand`, run by `r_parse_source` after `r_derive_expand`, appends an
`async i32 main()` generated from the tokens of the test declarations, one region per test so
that a diagnostic of the generated code is reported once at the test's attribute. Each test runs
inside a `try` that catches `std.test::failure` and `std.error::fault`, nested in a `try` that
catches the errors the test declares; catches of errors that the body cannot throw are allowed
(R-ERR-0003), so no generated catch can clash with a declared one. An allocation test repeats
the call with `std.test::fail_allocation_at(N)` for each attempt N of its passing run. A main of
the entry module is rejected with `R-DIAG-FLOW-001`. The allocation operations of `std.test`
(Library R-SLIB-TEST-0003) are table operations of `standard_async_sync.h` in the module
`test`, with the new shape `R_ASYNC_SYNC_SET_U64`; they lower to `r_std_test_*` calls
(`uses_std_test`, `r_std_test.h`, the link plan target `r_std_test`) over the attempt counter of
the hosted allocator. The tests of the library found ten defects, six of them in the compiler.
M24-1: the link plan of a synchronous program that called an operation of that table named no
library for it, because only asynchronous programs brought in `r_std_async`; the plan now names
the library of each table operation by module. M24-2: an owned `std.string::string` key (Library
R-LIB-0020) failed the core key preflight and had no C17 key helpers, and the dictionaries of
`std.env` hashed with SipHash; the key now has the contract of `str` (FNV-1a over the UTF-8
bytes, mixed with the seed) everywhere. M24-5: the overload `append` asked
`r_standard_query_call` for the type of its value, which knew only standard operations; the
query now takes the declared result of an ordinary R function or method, `Owner::method`
included (`r_generic_associated_method_quiet`), and a value whose type only its lowering knows,
such as a generic call, is lowered once and handed to the call through
`body->prelowered_result`. M24-6: `r_body_named_function_item` stopped at any earlier diagnostic
of the body, so an enum constant in a later case label had no value and the switch reported
repeated and missing cases; it now ends only on a diagnostic of its own resolution, a case
without a known value takes part in no repetition or exhaustiveness check, and
`r_generic_infer_call` infers nothing from an argument that an earlier diagnostic left without a
value. M24-10: `std.module::type::method(receiver, ...)`, the form of R-NAME-0010 for a method
of a standard type, was taken for a module path; `r_standard_lower_type_method` lowers it to the
operation of the receiver form, with the receiver at the argument index of that operation. The
other defects were in R modules: `std.deque` (M24-3), `std.sorted` (M24-4), `std.xml` (M24-7),
`std.bufio` (M24-8) and `std.args` (M24-9). See `tests/m24_regression_tests.inc`, the fixtures
`codegen_test_mode.r`, `codegen_string_keys.r`, `regression_test_mode_main.r`,
`regression_link_plan_table_operations.r` and `regression_rejected_parameter_cascade.r`, the
tests of `library/r/tests`, and the example `testing`.

### Constant size parameters

`@generic<T, const usize N>` combines type parameters and compile-time dimensions.
`Buffer<u8, 64usize>` has a closed layout, and a generic function infers `N` from `T[N]`
or nested generic arguments. Constant formulas are checked for range, overflow and
zero divisors; fixed arrays remain nonempty. Inference does not solve `N + 1` equations,
use the expected result or accept explicit generic-function argument lists.

Definitions retain an interned dependent expression DAG. Closing substitutes typed
constants, folds body uses and layouts, and reuses the ordinary monomorphization cache.
No runtime arguments or metadata are added. Core draft.58 admits every integer type and `bool`
as a constant-parameter type: a literal `CONSTANT_EXPR` keeps its type keyword in `flags` and
its value bits in `length`, an argument of another type is rejected rather than converted,
and the identity of an instance includes each constant's type. Interface schema 31 carries
`constant_type=u32`, typed constant arguments such as `(constant u32 15)` and dependent
formulas. The preflight `frame` example uses this for a bounded
wire frame with checked `@noalloc @nonblocking` packing and checksum helpers.

Core draft.57 lets such a formula call a function over the parameters (`u8[size_of::<T>()]`).
`r_generic_bound` turns it into a computed bound: a `CONSTANT_EXPR` node flagged
`R_TOKEN_LPAREN` whose identity is the interned formula (module and tokens, the declaration's
parameters spelled by position) and whose chain of `R_TOKEN_COMMA` links carries the
declaration's own arguments through substitution. `r_generic_canonical_constant` evaluates a
closed bound with `r_consteval_computed_value`: the formula's checked HIR, lowered once in the
declaration's scope after signatures exist and checked there by
`r_consteval_check_computed_bounds`, runs for the instance's arguments like a dependent `@if`
condition. An instance closed before bodies exist (a field of a non-generic struct, a signature)
takes the value a discovery pass computed, keyed by formula and argument spelling; in that pass
the instance is poisoned until the value exists. A formula that measures its own instance fails,
because an instance being completed has no layout (`RSemanticAggregate.completing`).

Core draft.58 adds associated constants (R-TYPE-0050). `traits.inc` collects the
`const T NAME [= value];` members of a trait into `semantic_associated_constants` and the
bindings of each implementation into `semantic_constant_bindings`, one slot per constant of
its trait; missing, unknown, re-typed and duplicate bindings are diagnosed there.
`r_body_associated_constant` resolves `Self::NAME`, `P::NAME` and `Type::NAME` with
`r_semantic_find_constant`: through the constraints of a parameter or the implementations of a
closed type. Over a dependent owner it yields a `CONSTANT_EXPR` flagged `R_TOKEN_DOT` whose
`length` packs the trait and the constant index and whose `R_TOKEN_COMMA` chain carries the
owner and the trait arguments; substitution closes it and `r_generic_canonical_constant` takes
the value from `r_consteval_projection_value`. A closed value is `r_consteval_constant_value`:
the implementation's formula or the trait default, registered as a computed formula of its
declaration and evaluated for the closed arguments. Implementations are collected after the
types that may use their values, so a module-scope use before `impls_ready` (`u8[Point::SIZE]`,
a field `Frame<Point>`) takes the value from the discovery pass, and
`r_consteval_check_impl_constants` evaluates the constants of every closed implementation.

### Translation-time evaluation

Core draft.54 evaluates ordinary functions during translation (R-FUNC-0023, R-EXPR-0032);
`semantic/consteval.inc` implements it. A function is evaluable when its checked HIR stays in
a closed subset: integers, `bool`, `char`, fieldless enums, string literals, fixed arrays and
drop-free structs of them, borrows and slices of local storage, loops, `switch`, `panic`,
`core::wrapping_*`/`saturating_*`/`enum_name`/`enum_ordinal` and calls of evaluable functions.
`r_consteval_classify` derives this from the body and every callee and memoizes it on the
symbol with the first reason against it; any static or thread-local variable, allocation,
async, `unsafe`, FFI, checked error or float keeps a run-time function. The call graph is
acyclic, so the interpreter addresses locals by symbol and keeps values in an arena.

Evaluation mirrors the run-time checks of the generated C (overflow, division, shifts,
checked conversions, bounds) and the target layout of `r_generic_layout`. Results become
literal, string, enumerator, array and aggregate HIR. Where a constant is required (array
bounds, constant generic arguments, enumerator values, case labels, module, `static` and
`thread_local` initializers) a panic is `R-DIAG-CONST-003`, the budget `R-DIAG-LIMIT-001`
and a run-time callee `R-DIAG-CONST-002` with the call chain. After checking,
`r_consteval_fold_program` replaces every call with constant arguments by its value; a call
that would panic or exceed the budget stays a run-time call. Module constants whose
initializer is not a literal operator expression are evaluated on first use.

Module-scope types are checked before bodies exist, so `r_consteval_needs_discovery` scans the
AST for a call or such a constant in a type or enumerator outside bodies; `r_consteval_discover`
then analyzes a second context over the same sources, evaluates those values once bodies are
available and injects them by AST node, repeating while new values appear. Documented limits
(R-IDB-010): 4000000 steps and 64 MiB of values per evaluation, at most 32 discovery passes
(a program whose module-scope values still grow after them gets `R-DIAG-LIMIT-001`),
and at most 256 scalar elements substituted inside a function body (a module constant holds a
larger value as one static initializer, wrapped before column 100). Interface schema 31 marks
evaluable exported functions `consteval=true` and records source dependencies whenever a
translation-time value was computed. The [tables example](../examples/tables/README.md)
builds a CRC-32 table, a frame size used by a module-scope struct, enumerator values and a
checked configuration.

Core draft.55 accepts constant conditions in `@if` (R-META-0002, R-META-0003). A static atom
that is not `Type is ...` or `core::profile`/`core::target is ...` is one condition leaf of
R-STMT-0002 (`true`, `false`, a comparison or a membership test); the parser tells them apart by
`is` after a type. In a body `r_static_resolve_values` lowers each leaf in the scope of the
`@if`: a closed leaf is a required constant (`r_consteval_condition`) and becomes a constant
predicate, so only the selected block is lowered. A leaf that names a constant generic parameter
or `sizeof`/`alignof` of a type parameter passes the constness check with `generic` set, keeps
its checked HIR and leaves both branches checked; each instantiation decides it in
`r_consteval_static_choice`, which interprets that HIR with the instance arguments substituted,
caches the choice per instance and reports a failure once, naming the instance (`pick<0usize>`).
A call of a generic function whose generic arguments depend on the definition's parameters
(`size_of::<T>()`) is dependent too: before evaluation `r_consteval_close_calls` closes each
such callee with `r_generic_close_callee` (the same step as cloning uses), builds its body and
proves it evaluable, so `R-DIAG-CONST-002` for a callee that is not names the instance. A local
read only by a condition or an array bound is marked `translation_time_read`, which acknowledges
it for R-FUNC-0020. Module conditions select declarations before any is collected, so when the
AST has one, `r_consteval_discover` runs before `r_static_select_modules`: a discovery pass
records each unknown condition as a request, selects neither branch and evaluates the request
with `r_body_lower_condition` once bodies exist; a condition over a declaration that another
condition selects is resolved by a later pass. The main analysis only reads the values and
reports the recorded diagnostic of a condition without one.

### Function items and generic error sets

A closed safe R function name forms a Copy callable value: `auto operation = increment;`.
It can be called directly or through `.call`, passed to a generic callable constraint and
returned through `opaque`. A synchronous item supports shared, mutable and consuming
callable modes; an async item supports the consuming async mode. Invocation targets the
original declaration, preserving static storage, checked errors, borrowing and the static
call graph. Open generic names, overload families, unsafe/C functions, variadic functions
and methods with an implicit receiver need an explicit adapter.

`@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>` infers the entire
checked-error set, including the empty set. `throws E, LocalFailure` forms a normalized
union. `E: error` still means one error payload type. Abstract sets cannot be caught as
values; named catches work after substitution and preserve cleanup and borrowed payload
origins. Async set parameters also require `send`; startup errors are declared separately.

### Exchange and cloning

`core::swap(&a, &b)` exchanges two initialized places of one replaceable type (the type rules of
`core::replace`) through one temporary with copy or move glue; nothing is dropped or allocated,
and borrowing one place twice is the ordinary argument conflict. Elements of one slice are
exchanged by `std.slice::swap`, which uses one-element raw slices internally for Move elements.

`core::clone(&value)` of a Copy value lowers to a read of the place (views keep their content
provenance). Any other cloneable type (`clone` capability, `r_clone_capability` in
`compiler/semantic/clone.inc`) is unborrowed and lowers to `STANDARD_CALL core::clone`, which C17
turns into generated per-type glue `r_type_clone_<type>` (`compiler/codegen/clone.inc`): strings,
paths, `array`, `list`, `dict`, `o`, `own`, fixed arrays, tuples, shared owners and nominal types
with an associated `T T::clone(const T* value)` hook. The glue builds the copy in uninitialized
storage, destroys every built component on failure and reports `std.alloc::alloc_error` through
the call's carrier; a clone that cannot fail (Copy, shared owners, hooks without errors) has no
checked effect. The glue is spliced in after the function prototypes because it calls hooks. The
static call graph adds an edge to every hook the copied structure reaches, so a hook that clones
its own type through an owner is a recursive call chain. Interface schema 31 records the `clone`
hook and the `clone` constraint. See `tests/fixtures/codegen_swap_clone.r`,
`codegen_async_swap_clone.r`, the failure sweep `codegen_clone_failures.r` and
`examples/tournament`.

### Function values

`fn [@noalloc] [@nonblocking] (P...) -> R [throws(E...)]` and `async fn (P...) -> R` are
function types (R-TYPE-0054). The parser reads `fn` or `async fn` in a type position as a
callable constraint when balanced parentheses and `->` follow (`r_function_type_ahead`), so a
statement such as `fn(i32) -> i32 f = twice;` declares an object and `fn i32 add(...)` still
declares a lambda. `compiler/semantic/function_values.inc` interns the kind
`R_SEMANTIC_TYPE_FUNCTION` like a callable signature (base = result or effect carrier, second =
the parameter chain) and converts a function item where such a type is expected
(`r_function_value_convert`, from `r_body_finish_value`) into `R_HIR_FUNCTION_ADDRESS` typed with
the function type; a value with more guarantees becomes an `R_HIR_CAST`. A call binds to the
prototype `call` of the callable trait of the type's own signature, applied to the function type
(the dispatcher, `r_function_value_call`); the receiver is taken by value, so temporaries and
borrowed values are called alike, and the dispatcher's result never borrows from it. A generic
constraint `fn(...)` accepts a function type of its signature, and a clone of a generic body binds
the call to the dispatcher of the concrete type in the constraint's mode.

After every body is built and closed, `r_function_value_resolve_targets` reads the conversions
(function addresses) and the casts between function types from the HIR and gives each dispatcher
the functions converted to its type or to a type that converts to it, with the span of the
conversion, as dyn targets. The stack graph and the resource proofs therefore follow a call
through a value to every target, and a cycle is reported at the conversion that closes it.
`r_function_value_build_dispatcher` gives each dispatcher a HIR declaration with its receiver and
parameters. C17 represents a function value as the `uint32_t` symbol number of its target
(`r_c17_type` maps the kind to `u32`); a synchronous dispatcher switches on it with a direct call
of each target, and an async start selects the target's frame initializer and launch like a dyn
dispatcher, with the arguments after the receiver. Interface schema 31 writes the type as
`(fn parameters=(...) return=R throws=(...))` with `async=true`, `noalloc=true` and
`nonblocking=true` when they apply.

### Derived implementations

`@derive(clone, equal, ordered, key, format)` before a module-scope struct, enum or error is expanded
from tokens before the source is parsed (`compiler/parser/derive.inc`, called from
`r_parse_source`): the declaration's fields, variants, generic header and, for an error with a
parent, the parent's fields (also from another module) give ordinary R text, an implementation
of `std.cmp::Equal`, `std.cmp::Ordered` or `core::Format` or the `T::hash`/`T::equal` hooks,
appended after the
authored bytes and lexed there (`r_lex_append`). Each capability is one region of the source
(`RDeriveRegion`); `r_add_diagnostic_phase` reports a diagnostic from a region at the capability
name and keeps one per region, and positions inside a region resolve to that name. CST and AST
dumps omit the regions, so example syntax coverage counts only authored nodes. Generated names use
a prefix that no identifier of the source starts with, and a module deriving equal or ordered gets
an implicit `import std.cmp;` during the interface scan. `clone` generates nothing: it sets
`clone_derived`, which makes the structural clone glue of `compiler/codegen/clone.inc` apply.
`compiler/semantic/derive.inc` records the capabilities, rejects unknown, repeated and misplaced
derivations and errors with descendants, and checks that every field or payload proves the
capability, naming the first that does not and silencing its region. Interface schema 31 writes
`derived=(...)` on the aggregate.

The generated text relies on three general mechanisms. An implementation whose target is a
standard type constructor over its parameters (`o<T>`, `array<T>`, `T[N]`, `T[]`, `const T[]`)
applies to every instance of that shape (`r_semantic_structural_impl` in
`compiler/semantic/traits.inc`); std.cmp uses it for options, arrays and slices, and implements
the traits for `str`, strings and tuples of two to eight elements. A generic implementation
applies only to instances whose arguments prove the constraints it adds for plain traits. A generic
hash, equal or clone hook may add capability constraints to the type's parameters; an instance
whose arguments do not prove them has no such hook (`r_generic_hook_applies`). See
`tests/fixtures/codegen_derive.r`, `codegen_derive_modules.r`, `codegen_async_derive.r`,
`codegen_cmp_impls.r`, `tests/check_derive.py` and `examples/tournament`.

### Output parameters

`void split(out i32 first, out i32 second)` declares output slots; callers write
`split(out left, out right)`. Each slot starts uninitialized in private callee storage.
Every successful return initializes every output, runs active `finally` clauses, then
publishes all outputs and destroys replaced caller values. A checked error destroys
private initialized outputs and leaves caller destinations unchanged. Destination
addresses are evaluated once in argument order; overlapping outputs are rejected.

Outputs currently require synchronous R functions and complete unborrowed value types.
The mode survives generics, traits, callable constraints (`fn(out i32) -> void`),
function items, opaque and module interface schema 31 (`(out i32)`). Async/C boundaries
and variadic outputs are rejected. The existing ban on errors escaping `finally` remains.

Each output write to local storage must be used before an overlapping overwrite or normal
scope exit, including branch, loop, catch and finally paths. Forwarding and explicit `as void`
acknowledge it; implicit normal destruction does not. Errors that unwind its scope, panic
and cancellation waive the obligation. `@discardable` on a named call permits
unused outputs, including a void function with outputs, unless an output type is
`@must_use`. Callable erasure does not preserve this permission. The analysis conservatively
handles runtime index equality and correlations between independent conditions.
See `tests/fixtures/codegen_out.r` and `examples/statistics/src/analysis.r`.

### Lambdas and closures

`fn [shared|mut|once] Ret name(params) [move(names)] { ... }` declares a local
closure (R-FUNC-0015..0017). Its environment contains by-value captures listed in `move(...)`
and otherwise ordinary borrows. Copy captures preserve their source. A captured borrow
keeps its original type and provenance. Without an explicit mode, writes select mutable
access and read-only captures select shared access.

`shared` uses a const environment, `mut` uses an exclusive environment borrow, and `once`
passes the environment by value. A Move `once` closure is called as `(move work).call(...)`;
a Copy closure retains ordinary Copy behavior. Callable constraints include the exact mode:
`@generic<F: fn mut(i32) -> i32>` or `@generic<F: fn once() -> i32 & send & unborrowed>`.
The omitted constraint mode is shared. No mode adapters are implicit.

A synchronous closure may be declared inside an async function. Owned Send, unborrowed
captures may cross suspension or be passed to generic async functions and inferred generic
thread entries. Start failure retains named Move arguments; frame and thread cleanup drops
only initialized storage. Borrowed captures cannot remain live across await. Checked lambda bodies declare `throws` after captures; async lambdas use `async fn` and
only the consuming mode. Their captures, parameters and completion values require Send
and unborrowed. Synchronous borrowed results preserve ordinary input provenance. Lambdas
inside generic functions are checked at definition and their private environments close
with the enclosing schema; consuming an environment adds no arbitrary field moves.

Each closure is a nominal environment aggregate with a `call` method. Generic calls bind
to that method during monomorphization, keeping direct calls and the static call graph.
Creation and synchronous invocation add no heap allocation; async calls use normal task starts.
Callable constraints may carry `@noalloc @nonblocking` after `fn` and an exact
`throws(E1, E2)` suffix; async constraints start with `async fn`. Resource guarantees may
be forgotten, never silently added. Interface schema 31 serializes mode, parameters, result,
resource promises, checked errors and async status and excludes synthetic traits.

Stored non-void call and await results are significant by default. `@must_use` additionally
marks named functions and nominal types. Each successful initialization or permitted write
creates its own obligation; every reachable normal path must acknowledge that value before
it is replaced or leaves scope. Ordinary reads, projections, borrows, forwarding, explicit
drop and `as void` acknowledge it. Short-circuit truth paths remain distinct. Errors that
unwind the storage, panic and cancellation are exempt; an ordinary early return is not.
An error caught while the binding remains alive preserves its obligation. Move discard
consumes the operand and invokes normal cleanup exactly once. Copy/Move, borrow checking
and task-resolution contracts remain in force; arbitrary copies do not inherit a linear
obligation. Unused results require `R-DIAG-USE-001`.

R-INIT-0014 separately rejects unconditional simple replacement of an initialized whole
local or value parameter with `R-DIAG-USE-002`, even after a read. Use a new local name for
a new value. Runtime arms, loop bodies/increments and catch handlers allow updates to an
outer binding. Ordinary blocks, try/finally and constant conditions do not. Compound updates,
field/index/pointer writes, moved-storage reinitialization and output initialization/publication
retain their existing rules.

`@discardable` (R-FUNC-0021) marks a named function whose result the caller may ignore. A
direct call of such a function in statement position, or an await statement whose call form
names such an async function, is lowered as `expression as void`: the result is destroyed once
with ordinary cleanup. The permission covers only that statement-position discard: a task is
never discarded, checked errors and borrow checks are unchanged, and the attribute does not
follow the value into a local, an operand, a callable constraint or a raw signature. It cannot
be combined with `@must_use`, cannot return a `@must_use` type, and requires a non-void logical
result or synchronous void with outputs; trait implementations inherit it from the prototype. For compiler-recognized standard
operations the permission comes from the library inventory (`discardable_result`, Library
R-LIB-0026): `tools/generate_standard_operation_registry.py` emits
`compiler/semantic/standard_discardable_operations.generated.inc`, the enumerator table the
expression-statement check consults for `R_HIR_STANDARD_CALL`. R-source standard modules carry
`@discardable` in their declarations.

### Null-only overloads

`null_t` is a compile-time parameter marker for the literal `null`, optionally in
parentheses. It is an exact overload match; explicitly nullable pointer parameters
remain compatible matches. A nullable pointer variable, even when null, is not a
`null_t` argument. Other types and computed expressions are rejected.

The marker is permitted in native function and method parameter lists, including
async functions and explicit parameters of generic functions. It cannot be stored,
returned, packed into containers, or inferred as a generic value type. The parameter
name is not an accessible object. It has no source-visible layout and is not a C ABI type.

### Associated types, core traits, range-for and membership

`type Name;` inside a trait declares an associated type and `type Name = T;` binds it in an
implementation (R-TYPE-0045); the trait is a schema over `Self` followed by its associated
types, `Self::Name` names one inside the trait or implementation, and `P::Name` on a
constrained generic parameter is a projection that instantiation replaces by the bound type.
The compiler declares two core traits on their first mention (R-TYPE-0046): `core::Iterator`
with `o<Self::Item> next(Self* this)` and `core::Contains` with
`bool contains(const Self* this, const Self::Item* value)`; they are implemented for nominal
types of the implementing module and named in constraints as `@generic<I: core::Iterator>`.

`for (T name in iterable) block` (R-STMT-0014) iterates a range `lo..hi` of one integer
type, a borrowed sequence `&place` of a fixed array, slice or `array<T>` (loop variable
`const T*`, `T*` or a copied `T`; `auto` is `const T*`), a borrowed `list<T>` or
`dict<K, V>` through its standard cursor, or a standard cursor or `core::Iterator`
implementation given by value or advanced through an exclusive borrow. `a in b` and
`a not in b` (R-EXPR-0029) test a range (`a >= lo && a < hi`), a dict place
(`std.dict::contains`) or a `core::Contains` implementation (`b.contains(&a)`); a membership
test is a condition leaf of `if` and `while`. `in` is a keyword and `not` is contextual.

Internally a range-for lowers to the loop core shared with `while`: hidden locals hold the
counter and bound, the borrowed sequence or slice, or the iterator; the loop body binds the
loop variable in a prologue and then lowers the user's block, so the object-state fixed
point, `break`, `continue` and drops of R-STMT-0004 apply unchanged. An iterator loop reads
the tag of the hidden `o<Item>` through a shared borrow (`R_HIR_VARIANT_TAG`), breaks on
`o::none` and transfers the payload (`R_HIR_VARIANT_PAYLOAD`) into the loop variable; the C17
back end emits `.r_tag` and `.r_payload.r_some` accesses and direct calls only. A projection
is a synthetic generic parameter (`RGenericParameter.projection_base`) that
`r_generic_substitute_type` resolves through the implementation of the substituted base.
Hidden locals are named by symbol in the generated C (`r_hNNNNNNNN_MMMMMMMM`). The new
semantic layer lives in `compiler/semantic/iteration.inc`.

### Collection expressions and variadic parameters

`[e1, e2, ...]` and `[element for (T name in iterable) if (condition) ...]` build an
`array<T>`; `{k1: v1, ...}` and `{key: value for (T name in iterable) ...}` build a
`dict<K, V>` (R-EXPR-0030). A collection expression has no type of its own and appears only
where the destination type is known (an initializer, an assignment right operand, a field
initializer); every element, key and value is lowered with the element, key or value type as
its context. The expression desugars to a hidden container created with
`std.array::create` or `std.dict::create`, one `std.array::push` or `std.dict::insert` per
element (an entry with an equal key replaces the earlier value, which is dropped), and a move
of the hidden local into the result, so the insertion effects `std.array::push_error<T>` and
`std.dict::insert_error<K, V>` must be caught or declared. A comprehension is one element
followed by a `for` clause and further `for` and `if` clauses: each `for` clause reuses the
range-for header and lowering (`r_body_lower_for_in_parts` takes a body callback that appends
the next clause or the insertion to the loop body, so nested clauses add no block levels of
their own), and each `if` clause is an else-less `if` with the usual object-state merge. `[]`
is the empty array expression; `{}` stays an aggregate initializer. Collection expressions
can also occur in arguments and returns when the complete container type is known.

`i32 sum(i32... values)` declares a variadic parameter of type `const i32[]`
(R-FUNC-0018); it is the last parameter of an ordinary function that is neither generic,
`async`, `extern "C"` nor a `@callback`. A call packs the trailing arguments into a hidden
fixed array `[T; n]` of the caller (`$pack`, dropped after the call returns; Move elements
are written with `move`) and passes its shared slice; `sum()` passes an empty slice;
`sum(...operand)` forwards an existing slice, `array<T>` or fixed array as a shared slice
without copying and is the only packed argument of that call. The result of a call may not
borrow the storage of the pack (`R-DIAG-BORROW-002`), only views that its elements hold (L25,
below). Function records of variadic functions carry
`variadic=true` in the module interface (schema 25). The parser adds
`R_SYNTAX_COLLECTION_EXPRESSION`, `R_SYNTAX_DICT_EXPRESSION` with `R_SYNTAX_DICT_ENTRY`,
`R_SYNTAX_COMPREHENSION_FOR`, `R_SYNTAX_COMPREHENSION_IF` and `R_SYNTAX_SPREAD_ARGUMENT`; a
braced initializer becomes a dict expression when `:` follows its first non-designated item.
The new semantic layer lives in `compiler/semantic/collections.inc`.

### Borrows held by containers and views

Core draft.62 to draft.64 lower containers, results and exclusive borrows whose values hold
views in any form (R-BORROW-0018). An aggregate, container or view variable that holds views
has two parts: its storage (the symbol itself) and what it holds (its `borrow_origin`). A
borrow of such storage names both; a read through a view substitutes the storage by what it
holds (`r_body_read_view_value_origins`). Each borrow or slice local, `o<T*>` local and switch
payload binding records the storage it may designate (`view_target*`,
`payload_view_target*`), so stores through it reach that storage and exclusive access through
it conflicts only with that storage. An exclusive borrow or slice reached through a shared or
const path grants shared access only (R-BORROW-0002); forming a slice of storage checks the
active loans of that storage like `&place`. A loop body is lowered once: the reads and stores
of view-carrying symbols in it are recorded, and after the body the states the back edge
carries are widened through the stores until they stop growing, then the reads are checked
again (`compiler/semantic/loop_dataflow.inc`). A parameter whose value, or the storage its
borrow or slice designates, holds views gets a hidden content symbol (`parameter_content`, with
`content_parameter` pointing back): a view read from that storage names the content symbol, so
it survives exclusive access to the storage through the parameter, never expires in the body
and maps back to the parameter in stores (R-BORROW-0019), result and error contracts and the
async main arguments check (R-BORROW-0024).

Core draft.65 (L13) completes the forms around these rules. A borrow a call derives from an
argument whose referent holds exclusive views designates the storage those views designate
(`r_body_view_value_held_targets`), plus the argument's own storage only when that referent can
contain the result's referent as a subobject; so `*first_slot(&slots) = text` for
`array<(str)*> slots` writes the str the element borrows, and `std.array::push(first_list(&lists),
text)` grows the array the element borrows. A value read through a borrow a call returns
(`*pick(words)`) carries what the designated storage holds. A Move `switch` or `match` moves an
exclusive borrow or a mutable slice out of a variant payload with the payload's origins
(`r_semantic_type_is_exclusive_view_payload`); a slice payload carries origins like a borrow.
Since L14 MIR and C17 accept any proven Move payload that holds views, such as a struct with an
exclusive borrow or a container iterator.
A caught error that holds a container of borrows is rethrown or dropped like any Move error.

Core draft.66 (L14) makes an associated type independent of the region of a call (R-TYPE-0045).
A method implementing a prototype with a borrowed receiver whose result mentions an associated
type of its trait, `next` of `core::Iterator` included, returns no view of the storage its
receiver designates; `r_body_record_return_borrow_origin` rejects one. A call of such a method,
direct, through a prototype in a generic body or by range-for, maps the receiver's contribution
to what the receiver holds (`r_body_result_held_content`), so items outlive the next
exclusive borrow of the iterator; other arguments keep the whole-argument contract. An element
or range of a shared slice held in storage reached through a view (`this->items[i]`) lies in the
slice's region (`r_body_held_shared_slice_elements`, R-BORROW-0021), and a store through a view
parameter of a borrow of the storage it designates is rejected (R-BORROW-0019). Walks of the AST
and HIR trees admit four times the parser's nesting limit (`r_frontend_tree_depth_limit`),
because one level of source nesting becomes at most three tree levels; a limit exhausted after
the parser is diagnosed `R-DIAG-LIMIT-001`. The ABI verifier and the C bridge spell declarations
into buffers that grow on the context allocator.

Core draft.67 (L15) extends the inferred contracts of R-BORROW-0009 without region syntax
(interface schema 21). A borrow or slice input whose every contribution to an output is what
the storage it designates holds is `held` (`return_borrow_held_low/high`, `held_low/high` of
error mappings): the call maps it to what the argument holds (`r_body_call_input_is_held`,
`r_body_result_held_content`), also in return projections, in the storage a result designates
and in loop reads (`r_body_loop_reads_through`); associated results of R-TYPE-0045 are the same
mapping for the receiver. A store of another parameter's view into the storage a view parameter
designates is recorded as a store contract (`RStoreBorrowMapping`, `stores=(...)`) instead of
being rejected; a call applies it to the storage the target argument designates and to what the
exclusive views held there designate (`r_body_apply_call_stores`), and the source's storage stays
borrowed until the function returns (`frozen_parameters`). Owners carry the regions of their
payload (R-BORROW-0018): a dereference of `own`, `rc` or `arc` is a projection of the owner's
storage (`r_body_hir_is_owner_dereference`), shared owners keep the region fixed at creation
through `clone`, `downgrade`, `upgrade` and `try_unwrap`, and a store through `get_mut` may not
widen it. A value read through a view reads as what the storage holds only when the read lies in
that storage (`r_body_read_designation`).

Core draft.68 (L16) adds lending trait methods and associated types with type parameters
(interface schema 22). `@lending` on a trait prototype (`RSemanticSymbol.is_lending`, checked by
`r_semantic_validate_lending`, inherited by implementations) exempts the method from the
associated-result contract: `r_semantic_prototype_has_associated_result` skips it, so a call
keeps the ordinary whole-receiver mapping and the implementation may return views of the
receiver storage. `core::LendingIterator` is declared next to `core::Iterator`; range-for falls
back to it when a type has no `core::Iterator` (`iteration.inc`), and its items borrow the
hidden iterator. An associated type may declare parameters: they form a schema of their own
(`associated_parameters`, scoped to the member span by `r_generic_resolve_parameter`), a binding
with parameters is a `R_SEMANTIC_TYPE_TYPE_FUNCTION` (body plus schema) applied at once by
`r_semantic_reduce_associated`, and an application of an associated type of a trait or of a
projection, `P::Name<T>`, is a synthetic generic parameter (`application_base`,
`application_arguments`) that carries the constraints of its base, so every predicate treats it
as a parameter. Substitution follows its base and arguments and reduces it once the base is a
binding. Arguments prove the parameter constraints at formation, or in
`r_generic_validate_definitions` for applications formed before constraints are known; a
binding proves the associated type's constraints with its parameters abstract. The
associated-result contract also covers associated types a subtrait inherits and applications of
generic traits (`r_semantic_trait_has_associated_of`), and a receiver whose type holds no view
gives such a result nothing to hold (`r_body_result_held_content`).

Core draft.69 (L17) infers generic parameters from the expected type of a result. The call
expression's contextual type reaches `r_body_lower_resolved_call` from `r_body_lower_call` and
from the method-call path (`r_body_lower_method_call(_named)`); an awaited call takes
`awaited_result_type` instead. After the arguments and callable constraints,
`r_generic_infer_from_expected` unifies the function's result, or its task for an async call
that is not awaited, with that type on a copy of the argument vector and keeps only parameters
the arguments left open, so a result that does not fit infers nothing. The standard
constructors `std.array::create`/`with_capacity`, `std.list::create`,
`std.dict::create`/`with_capacity` and `std.sync::channel`/`sync_channel`/`once_lock` parse
without `::<...>` when only their value operands follow (a trial parse in
`r_parse_standard_type_call`); their lowering takes the operands from the expected type
(`r_body_standard_operand_from_context`) and reports R-TYPE-0036 without one.

Core draft.70 (L18) adds tuples and type packs (interface schema 23). A tuple type `(A, B)` is
the instance of a synthesized generic struct of its arity (`r_tuple_origin`,
`RSemanticAggregate.is_tuple`) with fields `0` to `n - 1`, so layout, moves, cleanup and element
borrows are those of a struct instance; `.N` and match fields `.0 =` select elements, and the
liveness scanners compare an element index with the field name (`r_body_member_token_matches`).
A pack `T...` (`RGenericParameter.is_pack`) has a tuple argument; after constraints resolve,
`r_pack_finalize_parameters` moves its constraints to an element carrier (`pack_element`), so
the pack proves only what a tuple of such elements proves. `compiler/semantic/packs.inc` holds
the synthetic parameters over a pack, an element (`pack_index`) or a prefix followed by the
elements from an offset (`pack_derived`, `pack_prefix`), which substitution reduces once the
pack becomes a tuple, and the length facts `@if (len(T...) OP N)` gives a branch
(`r_pack_refine_branch`). A spread of a tuple partitions it at once into hidden pieces
(`r_tuple_spread_lowered`); a spread of a pack whose length an instantiation decides becomes a
hidden object, `R_HIR_PACK_PARTITION`, and `R_HIR_PACK_ELEMENT`/`R_HIR_PACK_ARGS` arguments that
`r_generic_clone_node` expands into piece locals, spliced arguments and positional tuple
fields, and a failed async start drops the pieces. A callable constraint `fn(X, P...) -> R`
ends its parameter chain in a link of length one, which substitution splices
(`r_pack_substitute_expansion`) and `r_pack_infer_chain` matches against a signature.

Core draft.71 (L19) continues a call after a method declared `@chain` (R-FUNC-0024, interface
schema 25 `chain=true`). After a method-call suffix the parser takes further suffixes
(`r_parser_continue_chain`) by wrapping the completed postfix expression in the nodes of its
parenthesized form, `(a.f()).g()`, and marks the inner expression `implicit_chain`; lowering is
the parenthesized one, and the method call rejects a marked receiver expression unless the
resolved method is `@chain` (`r_body_chain_rejected`, also for standard methods, callable
fields and function items). `@chain` belongs to a synchronous method with a result
(`r_semantic_validate_chain`), and implementations inherit it from their prototypes. In an async
frame a consuming receiver that a call or an await produced is staged in a hidden frame local
like any argument (defect L19-2), and an argument-free attribute no longer takes a following
tuple type as its argument list (`r_parenthesized_tuple_ahead`, defect L19-1).

Core draft.72 (L20) changes selection. A switch or select clause without a terminator ends
with an implicit break: the parser no longer requires the clause terminator and
`r_body_lower_switch_clause` treats a missing one as `break`. A match arm may be a `throw`
statement, lowered as a value scope of the throw and a `never` placeholder
(`R_HIR_DEFAULT_VALUE` of type never, which C17 emits as no value). String labels of a `str`
scrutinee and labels of a `core::CaseMatcher` scrutinee (a fourth synthesized core trait with
associated type `Label` and `bool matches(const Self* this, Self::Label label)`) become match
patterns `R_MATCH_STRING`/`R_MATCH_MATCHER`, tested through `std.bytes::equal` or a call of
`matches`; a switch over them (`compiler/semantic/labels.inc`) computes the number of the first
matching clause and lowers the clauses as an integer switch with precomputed tags, as select
does. `std.text::ignore_ascii_case` returns `std.text::ascii_caseless`, the matcher of labels
without ASCII case.

Core draft.73 (L21) adds single inheritance between errors (R-AGG-0011). The parser reads
`error Name : Parent { fields }` when a name follows the colon; a primitive type there still
declares a fieldless error enum. `r_semantic_resolve_error_parents` checks the parent (a visible
non-generic error with fields, no cycle), member collection copies the parent's fields as the
prefix of the child, and `r_semantic_declare_error_families` gives each error with descendants a
synthesized tagged enum, its family, whose variants carry the exact members of the subtree (the
error first) and whose fields are those payloads, so Copy, drop and layout follow the members.
A declared type that names such an error resolves to the family (`r_semantic_error_value_type`);
a literal keeps the exact error, which widens by a `VARIANT` node, and the family of a
descendant widens through a match-like chain over its variants
(`compiler/semantic/error_families.inc`). A throws entry expands to the exact members, and
`r_semantic_error_catch_distance` selects in the innermost try with a matching clause the clause
of the nearest ancestor: in semantic analysis (`active_catch_groups`), in generic instances
(conditional exits carry their try in `catch_group`), in MIR and in C17, where a clause of a
family receives an exact error as its variant. `throw` of a family value moves the exact error
out of its variant like a match arm and throws it, so a rethrow keeps the concrete type; the
fields of the error are read through a shared borrow of the payload of variant zero, whose
common initial sequence every member shares. `std.error::fault` names the family of the
standard errors of the main boundary (`r_semantic_standard_fault_family`, built on first use),
and `std.error::from_fault` is an erasure without a runtime symbol that C17 expands from the
main-boundary table (`r_c17_emit_fault_portable`). Interface schema 31 writes `parent=` and
`(error_family T)`.

Core draft.74 (L22) widens translation-time evaluation (`compiler/semantic/consteval.inc`,
`consteval_containers.inc`). A throw sets the failure `R_CONST_FAILURE_THROW` with the error in
flight, and `r_consteval_try` selects the clause of the nearest ancestor, builds the family value
of a clause for an ancestor and runs `finally` with the throw, return or jump kept pending; an
error that leaves a required constant is reported by `R-DIAG-CONST-003` with its value. The body
of a required constant lowers with `translation_effects`, so its calls may throw without the
enclosing function declaring the errors. Floating values of a binary32 or binary64 format are
computed with the host's IEC 60559 arithmetic after checking `FLT_EVAL_METHOD`, with the exact
`std.math` operations and the correctly rounded `sqrt` and `remainder`, whose domain errors
throw `std.math::math_error`; a C floating value is materialized as a cast of an R literal. Tagged
enumerations, options and families are `R_CONST_VARIANT` cells, a switch binds variant payloads,
and the standard owners are `R_CONST_CONTAINER` cells (a string as its bytes, a dictionary as
its entries in insertion order, a list as references to node storage) with cursors for
iteration; capacity is not observed. Outside a required constant a call that throws is still
replaced by its value when it completes, but a call that allocates is not. A `const` object that
is not `thread_local` receives owners frozen as array initializers of the owner type
(`consteval_freeze`); C17 emits their storage before the object (`r_c17_emit_frozen_data`) with
descriptors that allocate nothing, lays out a dictionary's index by the runtime's probe and
hash (`r_c17_frozen_key_hash`) and never drops such an object. Module objects of tagged types
now have static initializers, and module objects of an error type with descendants hold its
family (`r_semantic_remap_module_object_families`).

Core draft.75 (L23) adds deadline blocks (R-STMT-0019). The parser reads the contextual
`deadline (value) block` like `select` (`r_deadline_ahead`), and `semantic/deadline.inc` lowers
it into a hidden local `$deadline`, initialized by the internal standard call
`std.async::deadline_enter`, and a try statement whose compiler-owned finally calls
`std.async::deadline_leave`, the machinery that also drains a task group; an instant value is
wrapped in `o::some`. The runtime keeps a deadline in every task (`RRuntimeTaskDeadline`), copies
the deadline of the executing task into each task it prepares, ends a bounded scope wait no
later than it (`r_runtime_task_scope_wait_until`) and gives generated code
`r_runtime_task_deadline_enter`, `_leave` and `_narrow`. C17 narrows the deadline structure of
every standard asynchronous operation before its start (`r_c17_emit_deadline_narrow`) in the
synchronous and async paths, the scoped operations and the JSON reader. A call may omit the
trailing deadline argument of the operations in `standard_deadline_operations.generated.inc`,
which `tools/generate_standard_operation_registry.py` derives from the library inventory:
`r_standard_complete_deadline` appends a synthesized `o::none` to the argument list before the
operation, or the operation behind a method, is lowered. Field selection on a caught
`std.fs::fs_error` now prepares its schema itself (L23-1).

Core draft.90 (L34) adds budget blocks (R-STMT-0020). The parser reads the contextual
`budget (value) block` like `deadline` (`r_budget_ahead`), and `semantic/budget.inc` checks that
the value is the struct `std.alloc::limits` of the R part of `std.alloc`
(`r_semantic_budget_limits_fields`) and lowers the block into a hidden local `$budget`, a usize
that the internal call `std.async::budget_enter` returns, and a try statement whose
compiler-owned finally calls `std.async::budget_leave`. C17 reads the fields `bytes` and `tasks`
of the limits value and passes them to `r_runtime_task_budget_enter`, which returns the previous
budget of the task. The runtime keeps the budgets next to the allocator (`r_runtime_budget.h`):
each budget counts bytes and tasks against its limits and those of its parents. The task runtime
installs the budget of each task for its steps, copies it into each task it prepares and counts
an R task until it is destroyed. The allocator charges each allocation made under a budget and
records it in a sharded table, so a release returns its bytes on any thread and after the block.
A refusal sets a per-thread flag, through which `R_STD_ALLOC_REFUSAL()`,
`R_STD_ASYNC_START_REFUSAL()` and the generated start and list code report `budget_exhausted`.
`std.alloc::alloc_error` and `std.async::start_error` gain that variant.

Core draft.91 (L35) adds bounded recursion (R-FUNC-0026). `@recursion(depth = N)` on a
synchronous function sets `recursion_depth` of its symbol; the function must declare
`core::recursion_error`, a closed standard Copy error with the field `depth` (`r_core.h`,
`standard_errors.def`, the copy ABI table and a hidden field schema). `semantic/recursion.inc`
lowers the body as a statement `core::recursion_enter`, a checked standard call, followed by a
try statement over the function body whose compiler-owned finally calls
`core::recursion_leave`; `r_body_lower_try_statement` accepts the function declaration for it
and lowers its block as the function body. The stack graph check finds strongly connected sets
of functions (`r_stack_graph_components`): calls inside a set whose members all have the
attribute are skipped, and a set with a member without it is reported through a cycle that
names such a member. C17 (`codegen/recursion.inc`) gives each instance a thread-local counter
`r_activations_<ordinal>`, throws `RCoreRecursionError {.depth = N}` when the counter is at N
and marks the entry `/* R_STACK_RECURSION: N */`. `tools/compute_stack_entries.py` reads the
marks, accepts the cycles of marked functions, counts N frames of each member of a set and
records each set as `R_STACK_RECURSION_<first member>` in the stack header. A definition
without a frame of its own, one the C compiler inlined, keeps its calls in the graph. The proof
of `@noalloc` and `@nonblocking` accepts a call back into a function with `@recursion` whose
body it is proving (L35-1).

Core draft.92 (L36) adds asynchronous iteration (R-STMT-0021, R-TYPE-0046). The core trait
`core::AsyncIterator` is declared with the other core traits in the hosted-native-async
profile (`r_semantic_declare_core_async_iterator_trait`): its synthetic `next` is `@scoped
async`, throws the members of `std.error::fault` as a declared `throws std.error::fault` does,
and its Self and Item parameters carry Send (Item also unborrowed). `iteration.inc` plans a
borrowed named local or parameter of type `std.sync::receiver<T>`, or of a type implementing
`core::AsyncIterator` and no synchronous iterator trait, as the form `R_ITERATION_ASYNC`
(`r_iteration_plan_async`). Each iteration (`r_iteration_async_next`) borrows the source
again and starts the next item: the receive start `r_body_sync_receive_start`, factored out
of the `std.sync::receive` call, or an `R_HIR_ASYNC_START` of the scoped `next` recorded as a
loan of the innermost group (`r_scope_record_call`). The start is awaited through the helpers
`r_body_await_materialize` (the hidden `$await` local) and `r_body_await_task_symbol`,
factored out of `await`, so loans end and effects propagate as for `await f(...)`; the payload
then binds as for a synchronous iterator.

Core draft.93 (L37) adds four statement forms and borrowed outcome switches. The parser reads
`auto (a, b) = value;` as `destructuring_declaration`, `name: while/for` as
`labeled_statement`, an optional label after `break` and `continue`, `...base` as the last
item of a braced initializer, and `value is pattern` inside an `if` or `while` condition as
`pattern_test`. Destructuring (`r_tuple_lower_destructuring`, R-STMT-0022) evaluates the
tuple into a hidden object and splices one local per element into the statements of the
enclosing block or clause (`RBodyContext.statement_splice`). A struct update (R-INIT-0004)
stages the explicit values, evaluates the base into the hidden `$base`, transfers the omitted
fields, moves the replaced ones into `$replaced`, which is dropped, and forgets the rest of the
base. A labeled jump (R-STMT-0004, `semantic/loop_labels.inc`) whose label names the loop an
ordinary jump would take is that jump; any other is lowered by `r_body_lower_targeted_jump`:
the drops and finally routes of every scope up to the target loop, the object states recorded
into that loop's break or continue states, and an `R_HIR_BREAK` or `R_HIR_CONTINUE` whose
`loop_target` counts the target among the enclosing loops. MIR keeps a stack of loop targets,
C17 jumps to the loop's break or continue label, translation-time evaluation and the result-use
check count the loops left, and the startup check treats such a jump as reaching every enclosing
loop. A pattern condition (R-STMT-0002, `semantic/pattern_conditions.inc`) reuses the `match`
machinery: the tested value is a place or a hidden `$match` object, `if` tests it once and
`while` lowers to a loop whose body tests it, leaves the loop on a mismatch, binds and runs the
block. A switch on a place of a closed standard outcome without outer `move` borrows it like an
`o` (R-STMT-0010, L37.5): C17 copies the outcome bitwise and builds each binding's payload in a
shadow `r_view` value with the code of the moving switch, which is never dropped; in an async
function the payload borrow of MIR gets a companion `_view` field in the frame or the resume
stack.

Core draft.76 (L24) lets `std.async::detach(move member)` consume a member of a task group
without ending its supervision: the escape check of `semantic/task_scope.inc` treats detach like a
consuming cancel, and the runtime already frees the slot of an unobserved member when it is
published. `std.sync::receive` (Library R-LIB-0016) is lowered by
`r_body_lower_standard_sync_receive_call` into the standard call `R_STANDARD_CALL_SYNC_RECEIVE`,
which the profile gate keeps to hosted-native-async (the element type is read before new types are
interned, which may move the type table: L24-2); C17 passes the receiver borrow and a layout
of `o<T>` (`RStdSyncReceiveLayout`: tag and payload offsets) to `r_std_sync_receive`, which starts
an external task that waits in the FIFO of the channel (`channel_receive.c`) and writes the
option itself. A string label compared with a `constexpr str` subject, such as the name of a
portable error, now becomes a byte view through `str` as a call-bounded operand (L24-1,
`r_label_string_test`). `std.service`, the TCP service of Library R-SLIB-SERVICE-0001..0003, is an
R-source module (`library/r/std/service.r`) and needs no compiler support.

Core draft.77 (L25) adds three group and error forms. A variadic parameter may hold shared views
(`str... words`, `const T*...`, `const T[]...`) and a spread may pass a subrange,
`f(...arguments[1..])` (R-FUNC-0018); `r_body_pack_variadic_arguments` merges the borrow origins
of the packed arguments into the hidden pack, so a result that borrows an element borrows every
argument. The capacity of `task_scope(N)` may be spelled over constant generic parameters
(R-STMT-0017): `r_scope_lower` keeps a dependent capacity as a reference in the
`aggregate_member` of `R_HIR_TASK_SCOPE_ENTER`, and `r_generic_clone_node` evaluates it for each
instance through `r_consteval_group_capacity`, which reports a value outside 1..65536 with the
instance name. `await group.vacancy()` and `await group.vacancy_until(deadline)` (R-STMT-0018)
are group waits with flag 8, lowered to `r_runtime_task_scope_wait_vacancy` and
`r_runtime_task_scope_wait_vacancy_until`, which are ready once an entry of the group has no
task; `std.service` uses the barrier for its `overflow::wait` policy. Error exits share paths:
`r_mir_dispatch_effect_carrier` groups the members of a carrier that the function does not catch,
or that the same catch receives as a family value, when their cleanup blocks are the same tree
(`r_hir_same_tree`). Such a group leaves through one `throw` whose `runtime_type` names the
callee carrier; the carrier itself is the pending payload across finally bodies, and C17 moves
its error with one relay per carrier pair (`r_effect_relay_<n>`, spliced before the container
helpers) into the completion carrier or the catch binding. Synchronous calls
(`r_c17_group_effect_exits`) group the same way when no finally lies between the call and the
destination. An await of a call that throws all of `std.error::fault` therefore adds one path,
not 24.

### Other forms completed by L13

`values[i]` on an `array<T>` designates the live element in the buffer, with the borrow relation of
`std.array::get`/`get_mut`, in synchronous and async frames (`codegen_array_index`). An async
frame dereferences a borrow a call returns through a hidden frame local (`deref_local`), which
R-BORROW-0024 still keeps from crossing an await. Every borrow a scoped child thread takes is a
loan of a hidden `$thread_scope` group: an exclusive borrow or slice suspends all parent access
and a shared one every parent write, until the region completes or the exact handle is joined
(R-MEM-0015). `fallthrough` in a switch over a tagged enum or `o<T>` enters the next clause when
that clause binds no payload (R-STMT-0007).

`never` is a complete object type (R-TYPE-0007): a local, parameter, struct field or variant
payload may have it, only an expression that does not complete initializes it, and its
declaration ends the reachable path. C17 stores it as a placeholder byte (`uint8_t`) that only
unreachable code names; a read produces no value, and a call, variant or aggregate with a never
operand becomes a zero placeholder of its own type (`r_c17_emit_never_placeholder`,
`r_mir_lower_never_operand_completion`). `r_semantic_type_is_inhabited` treats `never`, a struct
with an uninhabited field and a tagged enum whose every variant carries one as uninhabited;
containers (R-TYPE-0012), checked errors, `arc`/`rc` (R-TYPE-0025), `task` (R-TYPE-0029) and
generic arguments (R-TYPE-0031) require inhabited types. An async never function is awaited only
by the call form; its C step writes no result. C imports, callbacks and raw C function pointers
with a never result are C `void` functions; a return from one reaches a
`contract_violation` panic at the call.

`atomic raw T*?` and `atomic raw const T*?` hold a nullable raw object pointer as
`_Atomic(r_dN)` with load, store, exchange and compare-exchange (fetch operations stay
integer-only). The object operand of a core atomic operation may name a mutable module object
without `unsafe` (R-OBJ-0009) when it projects only fields and fixed-array elements of that
static to its atomic object; the exemption is recorded while the operand is lowered and dropped
unless the lowered place has that shape.

### Standard modules written in R

`library/r/library.map` lists standard modules whose definition is an R source file
(`MODULE = PATH [PROFILE]`, paths relative to `library/r/`, the profile is the least
cumulative profile that provides the module and defaults to `freestanding`). The driver
option `--library-map FILE` loads such a module when a reachable `import std.name;` names
it and neither `--module-map` nor the closed C library provides it; the source is marked
`is_library`, so the reserved-root rule of R-MOD-0001 does not apply to it, and importing it
from a profile below its map entry is `R-DIAG-PROFILE-001` (Core R-MOD-0002, Library
R-SLIB-RSRC-0001/0002). Its traits, types, functions and methods then resolve like those of
any imported module and its generic definitions instantiate from the source under
R-TYPE-0040: the standard type and operation registries name compiler-recognized items
only, a registry miss for a loaded library module falls through to ordinary aggregate
resolution, and `std.name::Type<arguments>` parses as a generic type when `std.name` is a
loaded library module that declares the generic aggregate. Such a module has no C symbol of
its own: its items reach C17 only through the translation of the program that uses them.
The tests pass `-DLIBRARY_MAP=library/r/library.map` to the codegen, format and normative
drivers.

The shipped modules are `std.cmp` (the `ordering` enum, the `Equal`/`Ordered` traits with
`eq`/`cmp` implemented for `bool`, `char`, every integer type and `f32`/`f64` with a total
order, and `min`/`max`/`clamp`/`is_less`/`is_equal`), `std.slice` (`contains`, `index_of`,
`first`, `last`, `min_of`, `max_of`, `is_sorted`, `binary_search`, `swap`, `reverse`,
`sift_down` and an iterative heapsort `sort` over slices of Copy elements) and `std.text`
(`starts_with`, `ends_with`, `find`, `contains`, `count_byte`, `is_ascii`,
`to_ascii_lower`, `equal_ignore_ascii_case` over `str` bytes) in `freestanding`, and
`std.iter` (`of_slice`, `range`, `range_of_usize`; the adapters `map`, `filter_map`,
`take`, `skip`, `enumerate`, `zip`, `chain` whose iterator aggregates implement
`core::Iterator`; the consumers `count`, `fold`, `any`, `all`, `position`, `find_map`,
`last`, `nth`, `collect_array`, `collect_list`), `std.set::set<T: key>` over `dict<T, bool>`
with a `core::Iterator` over its keys, `std.deque::deque<T>` as two arrays (`heads` reversed,
`tails` in order, rebalanced when one side empties), `std.heap::heap<T: Ordered & copy>` as a
binary heap over `array<T>` and `std.sorted::set<K>`/`map<K, V>` as flat sorted arrays
with `lower_bound` binary search in `allocation`.

`std.service`, `std.stream`, `std.bufio` and `std.console` are `hosted-native-async`
R-source modules: a TCP service loop, the stream traits with their standard implementations, a
buffered reader and writer over any stream, and printing and line input on the console.

`std.regex` is an additional `hosted` R-source module: an immutable compiled Thompson NFA,
UTF-8 scalar matching, byte spans, leftmost-longest search, full matching, literal replacement
and splitting. Its checked errors distinguish syntax, unsupported constructs, compile limits,
execution budgets and allocation failures. It shares the existing module, ownership, async
and C17 paths and adds no native dependency. See [the executable example](../examples/regex/README.md).

Every item keeps the ordinary Core rules:
an item that grows a container declares that container's checked error, a returned borrow
borrows from the argument its signature names, and nothing recurses.

The compiler prerequisites of this layer are part of the same slice: aggregates with
borrow or slice fields are accepted in parameters, receivers and results, a passed
aggregate contributes the origins of its fields and a returned one inherits them
conservatively (`R-DIAG-BORROW-002` when such an aggregate would outlive its referent);
qualified trait names `@generic<T: std.cmp::Ordered>` resolve through the named module;
the schema prefix of an associated function is inferred from the owner spelling
(`set<i32>::create()`); a callable constraint `F: fn(I::Item) -> U` infers `U` from the
closure argument's signature and may live in the schema of a generic aggregate or
implementation; lambda parameters and callable-constraint parameters may be non-escaping
borrows or slices (`const T*`, `T*`, `const T[]`) while results stay borrow-free
(R-FUNC-0015, R-TYPE-0044); generic arguments of slice patterns accept borrowed fixed
arrays; hidden standard schemas (`entry_ref`, `alloc_error`, `target_info`, `json`, `net`)
are prepared on demand when a generic instantiation first names them; `len` applies to
`list` and `dict`. The library tooling records these modules as `implementation_language:
"r"` module records and `r_source` item records in `implementation_inventory.json`
(`generate_library_inventory.py` reads the map and the declarations, `check_library_layout.py`
validates that each source declares its module and items, and
`audit_library_source_surface.py` probes each item through `import` with the library map);
the generated registries skip `r_source` items.

### Static reflection

`core::enum_count::<T>()`, `core::enum_min::<T>()`, `core::enum_max::<T>()`, `core::enum_variants::<T>()`,
`core::variant_count::<T>()`, `core::field_count::<T>()`, `core::type_name::<T>()` and
`core::field_name::<T>(index)` are reflection constants (R-REFL-0001..0003, Library
R-LIB-0024): the parser recognizes the `core::name(type[, operand])` forms next to the
standard type calls (`R_SYNTAX_STANDARD_TYPE_CALL`), and `compiler/semantic/reflection.inc` folds them into ordinary HIR literals (a
`usize` literal, an enum constant, a fixed-array initializer of enum constants or a
program string), so neither MIR nor the emitter sees them. The canonical type spelling of
`type_name` is `module.path::Name<arguments>` for aggregates and the source spelling for
every other type (`const T*`, `T[]`, `array<T>`, `o<T>`, `raw fn(P) -> R`, ...).
`core::target_name()` and `core::profile_name()` fold to program strings from the generated
`target_identity.generated.inc` and the selected profile (R-REFL-0004). The five runtime
selections `core::enum_name(value)`, `core::enum_ordinal(value)`, `core::enum_at::<T>(index)`,
`core::enum_from_name::<T>(name)` and `core::variant_name(const T* value)` stay
`R_HIR_STANDARD_CALL` nodes; preflight records one helper per selection and enumeration,
and the emitter prints each once as a translation-unit-local static function
(`r_reflection_<selection>_a<aggregate>`) holding the `switch` over the enumeration value,
the index or the active tag that assigns the program string, the ordinal or the `o::some`
payload; `enum_from_name` holds a `static const` table of the variant names searched by the
shared `r_reflection_find_name` helper. Every call site, in the synchronous and in the async
path, is one assignment call of that helper. No runtime symbol, library symbol or run-time
metadata is involved, so the forms work in the freestanding profile (`str` operands
excepted). A form
over a generic parameter becomes a dependent standard call that the generic clone folds or
proves once the parameter is substituted (`r_reflection_fold_clone`), so `type_name(T)`
inside a generic body spells the instantiated type; `enum_variants` requires a concrete
enumeration. Wrong operand kinds are `R-DIAG-TYPE-001`, a field index beyond the field
count is `R-DIAG-CONST-001`. `examples/reflection` is the executable walk-through of every
form (CTest case `reflection_example`).

Valid constructs outside the implemented semantic or code-generation slices receive
`R-DIAG-SLICE-001` or make `r_frontend_emit_c17` return
`R_FRONTEND_NOT_LOWERABLE` before its writer is called. The M6 review classified every
such site: `panic(message)` (R-ERR-0004, category `explicit`, sync and async frames),
`never`-typed expression statements that end their path (R-TYPE-0029, R-FUNC-0003), open
ranges `a[lo..]`, `a[..hi]` and `a[..]` (R-EXPR-0021), module constant expressions over
literals and earlier constants (R-INIT-0002, overflow is `R-DIAG-CONST-001`), unary `+`,
`-` and `~` on small integers (R-EXPR-0003), `char` comparisons (R-EXPR-0009), and
`char`/integer and fieldless `@repr(C)` enum/integer conversions with `invalid_conversion`
panics for non-scalar code points and undeclared discriminants (R-EXPR-0016,
R-EXPR-0018) are lowered; ill-formed programs that used to receive a slice notice now get
the normative diagnostic (unknown type names, `void` fields, aggregate attributes, calls on
non-functions, non-numeric `as` operands, `await` into borrows, `drop` of views). Since Core
draft.65 (L13) specification section 25.1 lists no valid form that the compiler rejects; the
remaining slice sites (`tools/audit_semantic_slices.py`) and the not-lowerable branches of MIR
and C17 (`tools/audit_lowering_rejections.py`, L14.2) are guards, and a valid program that
reaches one is an implementation defect rather than a documented boundary. Direct users of the internal
C API lower MIR before requesting C17 whenever a reachable function is async; the
CLI performs this phase automatically. A successful `unzip` build is an integration
milestone, not yet a complete Core 0.1 conformance claim: remaining language/library
operations, full borrow/drop proofs, and the complete diagnostic coverage gates are still
tracked as unfinished work.

### Hosted bootstrap mapping

For the current Darwin bootstrap target, native `argv` elements are accepted exactly
when their existing bytes are well-formed UTF-8; no replacement or locale conversion
is performed. A launch with zero native arguments synthesizes `<program>` as element
zero. Startup reserves one backing allocation even when the R entry point has no
argument parameter. The allocation-free emergency sink is file descriptor 2 via the
native `write` operation. `argument_encoding_failure` uses process status `125`, and
startup-snapshot `allocation_failure` uses status `126`.

### Library profiles

`--profile` selects the cumulative library profile before translation (Core R-CONF-G005,
Library R-SLIB-PROFILE-0001) and defaults to `hosted-native-async`, the only profile with a
committed target manifest. A facility outside the selected profile is diagnosed with
`R-DIAG-PROFILE-001` instead of being linked to a stub:

| Profile | Adds | Rejected |
| --- | --- | --- |
| `freestanding` | language and `core` | `new`, `bytes`, `array`/`list`/`dict`, `arc`/`rc`/`weak`, every `std.*` item, `task`, `async`, `thread_scope` |
| `allocation` | `new`, containers, `std.alloc`, `std.arc`, `std.rc`, `std.array`, `std.list`, `std.dict` | remaining `std.*`, `task`, `async`, `thread_scope` |
| `hosted` | `std.error`, `std.bytes`, `std.hash`, `std.utf8`, `std.bits`, `std.string`, `std.convert`, `std.format`, `std.json`, `std.regex`, `std.math`, synchronous `std.time`, `std.env`, `std.c`, `std.secret`, `std.process::exit`/`abort` | `std.thread`, `std.sync`, `std.async`, `std.io`, `std.fs`, `std.net`, child-process `std.process`, `std.time::sleep_*`, `task`, `async`, `thread_scope` |
| `hosted-thread` | `std.thread`, `std.sync`, `thread_scope` | the asynchronous family |
| `hosted-native-async` | everything | nothing |

`std.json` is placed in `hosted` because it depends on `std.string`; the specification does
not assign it to a profile clause. Standard modules written in R carry their least profile
in `library/r/library.map` (`std.cmp`, `std.slice`, `std.text` in `freestanding`;
`std.iter`, `std.set`, `std.deque`, `std.heap`, `std.sorted` in `allocation`,
`std.regex` in `hosted`) and are gated
at their `import`. Enum-constant paths such as `std.fs::error_code::x` are
not gated on their own: every operation and type of the module already is. The gates are
exercised by the `r_frontend_profile_*` CTest cases; `examples/generics` builds under
`allocation`, and `examples/unzip` requires `hosted-native-async`.

### Freestanding profile

`--profile freestanding --target-manifest targets/arm64-apple-darwin.freestanding.json` compiles
a program against the language and `core` only (Core R-CONF-0005, Annex G.3). The generated C
includes `r_runtime_core.h`, `r_runtime_freestanding.h` and the freestanding
`r_runtime_target_abi.h` (generated from that manifest without `<wchar.h>` or the allocator
constant), defines no hosted `main`, and wraps neither its `@callback` entries nor its calls into
C in floating-environment guards. It links only the freestanding runtime
(`runtime/freestanding`, built with `-ffreestanding`: panic forwarding, environment-adopted stack
bounds, the thread-local destruction hook) and the environment that embeds it:

| Symbol | Defined by | Role |
| --- | --- | --- |
| `r_runtime_environment_panic(category, span)` | environment | receives every panic and never returns |
| `r_runtime_freestanding_stack_adopt(low, high)` | runtime | adopts the calling thread's stack before its first entry into R; `R_RUNTIME_STACK_PROTECTED_LOW_BYTES` above `low` stay unused |
| `r_runtime_freestanding_stack_release()` | runtime | forgets the bounds; later entries panic with `stack_exhaustion` |
| `r_runtime_freestanding_thread_exit()` | runtime | runs the program's thread-local destruction entry for the current thread |
| `r_freestanding_main()` | generated C | when the program defines `i32 main()`: stack preflight, static initialization, `main`, static drops, status |

Every `@callback` function is a further entry from C with its own stack preflight. Without an
allocator the profile also rejects `own` pointers and `core::adopt` (`R-DIAG-PROFILE-001`). The
emitter binds each compilation to the manifest of the selected profile: the digest table
`compiler/codegen/target_manifest_digests.generated.inc` lists the committed manifests, and every
hosted profile compiles against the hosted-native-async manifest. On Darwin a freestanding object
additionally needs the loader's `__tlv_bootstrap` for `_Thread_local` objects and the compiler's
`memcpy`/`memset`; `tests/check_freestanding_program.cmake` proves that no other symbol is needed
(`nm -u` against `tests/freestanding/allowed_undefined_symbols.txt`) and runs the program under a
hosted environment stub that captures panics.

### C imports

An `extern "C" { ... }` block imports C functions, objects, constants and opaque types
(R-FFI-0001, R-FFI-0013). Prototypes use C ABI scalars, raw pointers, raw C function types,
raw pointers to opaque C types, complete C aggregates declared in the block and `@repr(C)`
structs and enums declared in R; every function import carries `@safety`, optional
`@link_name` and optional `@fenv("preserve")`. The block carries an optional singleton
`@link(name = ..., kind = ...)` and at least one `@header(...)` or one `@abi(...)`
(`R-DIAG-FFI-006`). A call requires an `unsafe` context (`R-DIAG-UNSAFE-001 [R-FFI-0010]`)
and lowers to the same raw-function indirect call as a `raw fn` value: the C-boundary
floating environment is saved and restored around the call.

Every value that enters R from C is checked before it becomes an R value (R-FFI-0056,
R-CMAP-0029): a non-null raw pointer or function pointer shall not be null and a fieldless
enum shall name a declared variant, through every `@repr(C)` field and fixed-array element.
The check covers the result of every C call (an import or a `raw fn` value), every read of
an imported object or of its fields and elements, and every argument of a C-origin entry; a
violation is a `contract_violation` panic, which aborts at a C-origin entry (R-ERR-0006).

The other declaration forms of a block are:

- `opaque struct Name;` (R-FFI-0015) declares an incomplete nominal C type that is usable
  only behind `raw` pointers and raw function types; by-value use in a local, parameter,
  return type, field, `new` or dereference is `R-DIAG-FFI-003 [R-FFI-0015]`. `@c_type(name =
  "CName", kind = "struct" | "union" | "typedef")` (R-FFI-0016) selects the C spelling;
  without it the R name is the struct tag. A `kind = "enum"` opaque is rejected because C17
  has no incomplete enumerations.
- `@c_constant(name = "C_NAME") const CTYPE NAME = literal as CTYPE;` (R-FFI-0018) mirrors
  an integer macro or enumerator: CTYPE is a C ABI integer type, the initializer is one
  integer literal converted with `as` (a leading `-` inside or outside the conversion is
  allowed), and the value must be representable in CTYPE. The constant has no storage: every
  use is substituted as a literal (R-CMAP-0019).
- `TYPE name;` and `thread_local TYPE name;` (R-FFI-0022) import a C object with an exact
  C ABI type (`R-DIAG-FFI-001 [R-FFI-0022]` otherwise); `const TYPE` imports a read-only
  object. Every read or write needs an `unsafe` context (`R-DIAG-UNSAFE-001 [R-FFI-0023]`).
  Generated C declares `extern [_Thread_local] TYPE identifier;` and names the object
  directly; there is no mirrored storage. An object whose C name is reserved (R-FFI-0057) or
  whose type names an R-declared struct is reached through the accessor bridge instead:
  the bridge defines `TYPE *r_bridge_<identifier>(void) { return &identifier; }` (with a
  cast to the main unit's private tag) and the program accesses `(*r_bridge_<identifier>())`,
  so every access still reaches the actual object, and the calling thread's instance of a
  `thread_local` one; its block needs `@header` (`R-DIAG-FFI-003 [R-FFI-0057]`). The
  manifest inventory must list the object as `data`, or `tls` for `thread_local`
  (`R-DIAG-LINK-003`).
- `R name(P fixed, ...);` (R-FFI-0005) imports a variadic function. At least one fixed
  parameter is required by the grammar, the call passes the fixed arguments exactly and every
  additional argument must already have a promoted C ABI type: `c_int`, `c_uint`, `c_long`,
  `c_ulong`, `c_llong`, `c_ullong`, `c_double` or a raw pointer (`R-DIAG-FFI-002
  [R-FFI-0005]`). The raw function type of a variadic import carries an internal variadic
  flag (`variadic` in HIR and MIR dumps), so generated C calls through `R (*)(P, ...)`.
- `@repr(C) @c_type(name = "CName", kind = "struct" | "enum" | "typedef") struct/enum ...`
  (R-FFI-0017) declares a complete C aggregate whose member or enumerator inventory is proven
  one-for-one against an ABI record (R-FFI-0040/0041). The block must name the record with
  `@abi("record-name")`; a complete aggregate without `@abi`, without `@repr(C)` or `@c_type`,
  with a `union` spelling or with a kind that disagrees with the declaration is rejected
  (`R-DIAG-FFI-004`, `R-DIAG-FFI-003`). Such aggregates may pass by value, by raw pointer and
  as imported objects. Struct fields keep their C member names; an enum lists every C
  enumerator with its value and names the compatible integer type as its underlying type.
- A `@repr(C)` struct or fieldless enum declared in R (R-FFI-0004, R-FFI-0006) may occur in
  an import's prototype or an imported object's type by value, behind raw pointers and
  inside raw C function types. An enum is its compatible integer type in C (R-AGG-0005):
  the verifier's prototype assignment proves the header's enum compatible with it, and a
  value entering R is checked against its variants. A struct names no C type, so the C
  type at its position comes from the ABI record: the block needs `@abi`
  (`R-DIAG-FFI-004 [R-FFI-0041]`) and `@header` for the bridge (`R-DIAG-FFI-003
  [R-FFI-0057]`), and the record carries the import's C type as the compiler resolved it.
  The frontend walks the R prototype against that tree and proves each R-declared struct
  against the C struct at its position one-for-one: member count, names, order, member
  types (recursively for nested structs, pointers to structs, arrays and enums), offsets,
  size and alignment (`R-DIAG-FFI-004 [R-FFI-0041]`); a pointer or value where C has the
  other, or a union or an inexpressible C struct at such a position, is `R-DIAG-FFI-004
  [R-FFI-0021]`/`[R-FFI-0019]`. One R struct may correspond to different C structs in
  different imports. Every such import goes through its own bridge function
  `r_bridge_<identifier>_<symbol>`, which never defines the R struct: a struct passed by
  value travels by address and is copied into the header's type with `memcpy` after the
  layouts were proven identical, a result returns through a leading out-pointer, and a
  pointer or C function pointer is cast to the header's spelling. The main unit reaches such
  a bridge through a private thunk `r_thunk_<identifier>_<symbol>` with the import's own
  prototype, so calls and `raw fn` values of the import stay unchanged. A variadic import
  cannot pass an R-declared struct (`R-DIAG-FFI-003 [R-FFI-0057]`).

Generated C uses the direct declaration path of R-CMAP-0017 for an ordinary identifier:
one `extern` prototype with the actual C identifier and no definition. An opaque type with
a struct or union tag becomes `typedef struct CName r_aNNNNNNNN;` so the pointer types
agree with the header's own declaration, and a complete struct with a struct tag is emitted
as the complete normalized declaration `struct CName { ... };` from its verified members
(R-CMAP-0018); a complete enum is its compatible integer type, which C17 6.7.2.2 makes
compatible with the header's `enum CName`. An identifier in the C library or implementation
reserved classes (`abs`, `strlen`, `mem*`, `r_runtime_*`, ...) is never redeclared by
generated code (R-FFI-0057); its block needs at least one `@header`, and the program calls
the private `r_bridge_<identifier>` forwarding function of the bridge translation unit
instead. The same bridge carries every import whose signature mentions an opaque type
spelled by a header-owned `typedef` or by a reserved tag (R-CMAP-0018): the main unit keeps
a private incomplete tag `struct r_aNNNNNNNN`, and the bridge converts each such pointer to
the header spelling and back. A typedef-spelled complete struct keeps a private definition
`struct r_aNNNNNNNN { ... }` in both units, and the bridge copies a by-value argument or
result between the private and the header type with `memcpy` after the layouts were proven
identical. A variadic import cannot use the bridge.

ABI records are produced outside the compiler by `tools/generate_c_abi_record.py`:
`--emit=abi-inventory` writes the request (schema `r-abi-inventory-request-0.1`) listing,
per block that names an `@abi` record, its provider, feature-test definitions, headers, the
C types to inventory and the `symbols` (functions and objects) whose type passes an
R-declared struct; the tool compiles the headers with the target C compiler and derives the
member inventory and layouts from `-ast-dump=json` and `-fdump-record-layouts-complete`,
and the compatible integer type of each enumeration from `_Generic` static-assertion
probes, without executing anything on the host (R-CMAP-0024). The C type of each requested
symbol is read from a `__typeof__` probe of the tool's own translation unit and recorded as
a type tree of pointers, functions, arrays, scalars and struct, union and enumeration
leaves; every struct and enumeration reached from such a tree, directly or through members,
is inventoried with a type tree per member and a layout read from `sizeof`, `_Alignof` and
`offsetof` constant expressions, or marked `inexpressible` (bit-field, anonymous member,
union, incomplete type). The resulting document (schema `r-abi-record-0.1`, one entry per
record name with its compiler identity, options, definitions, header digests, `types[]`
and `symbols[]`) is passed back with `--abi-record FILE`. Before emission the frontend proves every C-declared aggregate against
it: member count, names, order, type spelling (`type` or `desugared_type`), offsets, size and
alignment for structs; the compatible integer type and every enumerator name and value for
enums, rejecting aliased enumerators. Any disagreement, a missing record or a missing type
is `R-DIAG-FFI-004 [R-FFI-0017]`/`[R-FFI-0041]`. Layout alone is never accepted: a C member
omitted by the R declaration fails the count even when it hides in padding. A used record
must also name the block's link provider, the target manifest's `target_triple` (when a
manifest is given) and C17 compiler options (`R-DIAG-FFI-004 [R-FFI-0040]`). Its compiler
identity shall name the manifest toolchain's `c_compiler_build`, and every header digest it
carries is re-hashed against the header found under the roots given with
`--abi-header-dir DIR` (repeatable, the same roots the record generator received as `-I`);
a stale digest, a header no root provides or a record without compiler identity is
`R-DIAG-FFI-004 [R-FFI-0044]`. The record document digest and every header digest enter
`--emit=interface` and `--emit=link-plan` as `(abi-record present=true sha256="...")` and
`(abi-header record="..." spelling="..." sha256="...")`, so a regenerated record changes the
module interface and build fingerprints (R-FFI-0044).

Managed-token adapters (R-FFI-0060, R-CMAP-0037) are ordinary exported `extern "C"` R
functions: create allocates an owner and applies `std.arc::into_raw`, converting the
`raw const T*` to `raw const void*` with an unsafe cast; retain reconstructs one obligation
with `std.arc::from_raw`, clones once and turns both owners back into obligations; release
reconstructs and drops one. The exported entry attaches the thread and canonicalizes the
floating environment before any pointer reconstruction. A C-origin entry also validates its
ingress: a non-nullable raw pointer or function pointer that arrives as NULL and an enum
whose value names no declared variant, in an argument or in any `@repr(C)` member of one,
abort with `contract_violation` (R-CMAP-0020). One C spelling imported from two link providers is rejected
(`R-DIAG-FFI-003 [R-FFI-0007]`). The R-CONF-G009 matrix over the probe library is recorded
in `docs/ffi-conformance-g009.md`.

Two more artifacts accompany `--emit=c17` for a program with imports:

- `--emit=abi-verifier` writes the R-FFI-0042 verifier translation unit: the providers'
  `feature_test_definitions` as `#define` lines, every block's `@header` spellings as
  `#include <...>` in source order (a repeated spelling once), one
  `R (*const r_verify_NNNNNNNN)(P...) = identifier;` assignment per function import (with
  `, ...` for a variadic one), `_Static_assert(_Generic(&(identifier), TYPE *: 1, default:
  0), ...)` per imported object, and for each `@c_constant` a value probe
  `_Static_assert((C_NAME) == ((CTYPE)VALUE), ...)` plus a type probe
  `_Static_assert(_Generic((C_NAME), CTYPE: 1, default: 0), ...)` (R-CMAP-0019), and for
  each C-declared aggregate the R-FFI-0042 layout probes: `sizeof`, `_Alignof`, `offsetof` and
  `_Generic` member-type probes of a struct (an array member through its address), the
  compatible-integer probe and one `_Static_assert` per enumerator of an enum. Each
  R-declared struct gets the same struct probes against the C struct its record position
  names, and the prototype spells that C struct at the position, so a record that names the
  wrong C type fails there. Opaque and complete types are spelled as
  `struct CName`, `enum CName` or the typedef name. The build compiles
  the unit for the target with the generated-C options and `-fsyntax-only`; a prototype,
  object type, tag kind or constant that disagrees with the header fails to compile, which
  is the `R-DIAG-FFI-004` outcome. The unit is never executed.
- `--emit=c17-bridge` writes the R-CMAP-0026 bridge translation unit: the same macros and
  headers, the private tags of typedef-spelled opaque types, and
  `R r_bridge_<identifier>(P a0, ...) { return identifier(a0, ...); }` for every bridged
  function import, deduplicated by identifier, with pointer casts between the private tag
  and the header spelling where needed. The build compiles and links it with the program.
  Both units contain only C ABI spellings and no R runtime header.

Header roots and include paths stay in the build manifest (R-FFI-0032): the test driver
passes `-I` for the provider's `header_roots`. Providers that are used together shall not
disagree on one feature-test macro (`R-DIAG-FFI-004 [R-FFI-0043]`).

Linking is resolved before emission by `r_frontend_resolve_links`, which the CLI runs for
`c17`, `link-plan`, `bundle` and `interface`. The link manifest names each provider with
`logical_name`, `kind`, `available`, an optional `implicit_c_runtime` flag for blocks
without `@link`, and a `symbols` inventory of `{c_identifier, kind, binding}` objects.
Unknown providers, unavailable artifacts, kind disagreement and symbols missing from the
inventory are `R-DIAG-LINK-001`, `R-DIAG-LINK-002` and `R-DIAG-LINK-003`. The link plan
lists every provider of a used function or object import as `(link name=... kind=...)`; a
constant selects its provider for header evidence only. The executable fixtures
`codegen_ffi_import`, `codegen_async_ffi_import`, `codegen_ffi_bridge`, `codegen_ffi_types`,
`codegen_async_ffi_types` and `codegen_ffi_records` link `tests/fixtures/ffi/probe_library.c`;
the last generates its ABI record at test time (`tests/abi_record_support.cmake`) and passes
a tag struct, a typedef struct through the bridge, an unsigned and a signed enum and a struct
object; the third reaches `abs` and
`strlen` through the bridge, and the last exercises an opaque struct tag, a typedef-spelled
opaque type through the bridge, four verified constants (including a negative macro, an
unsigned macro and an enumerator), a mutable, a `const` and a `thread_local` object and a
variadic call. `tests/check_abi_verifier.cmake` proves that a mismatched prototype
(`ffi_verify_mismatch`), a wrong tag kind (`ffi_verify_opaque_mismatch`), a wrong object
type (`ffi_verify_object_mismatch`) and a wrong constant value or type
(`ffi_verify_constant_mismatch`, `ffi_verify_constant_type_mismatch`) are rejected by the
verifier, and that a record whose claims disagree with the header (`ffi_verify_record_claim`)
fails its layout probes, as does a record that names the wrong C struct at the position of an
R-declared struct (`ffi_verify_r_struct_claim`). The `r_frontend_abi_record_*` tests reject an
omitted member, a wrong member type, a missing enumerator and a wrong enumeration
representation against the generated record, and for R-declared structs a wrong member
name, a member hidden in padding, a pointer where C passes by value and an enum member of
another integer type. `codegen_ffi_repr_c_imports`, `codegen_ffi_repr_c_objects` and
`codegen_async_ffi_repr_c` pass R-declared structs and enums by value, through pointers,
as results, inside callbacks and as imported objects (including a reserved-name object), and
the `*_ingress` fixtures panic on an invalid enum result, on an invalid enum and a null
pointer member of a struct result, on an invalid imported enum object (also in an async
frame) and, at the C-origin entry, on an invalid enum member of a struct argument.

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers

cmake --preset fuzz
cmake --build --preset fuzz
ctest --preset fuzz
```

The fuzz preset uses libFuzzer when the selected Clang distribution provides its
runtime. It otherwise builds a deterministic standalone arbitrary-byte driver,
still instrumented with AddressSanitizer and UndefinedBehaviorSanitizer.

Examples:

```sh
build/debug/r-front --emit=tokens source.r
build/debug/r-front --emit=cst --diagnostics=json source.r
build/debug/r-front --emit=ast \
  --module-map examples/unzip/modules.map
build/debug/r-front --emit=hir source.r
build/debug/r-front --emit=mir source.r
build/debug/r-front --emit=interface source.r
build/debug/r-front --emit=link-plan \
  --entry application.main --profile hosted \
  --target-manifest target.json --link-manifest links.json source.r
build/debug/r-front --emit=bundle \
  --entry application.main::main --profile hosted source.r
build/debug/r-front --emit=c17 source.r > generated.c
```

`--target-manifest` accepts only the committed manifest of the selected profile's target, byte
for byte, in every emit mode; any other manifest is refused before an artifact is written. With
`--module-map` and `--entry`, an import that no map entry declares is reported at the import as
`R-DIAG-MOD-001` (R-MOD-0007), and a cycle of imports as `R-DIAG-MOD-001` (R-MOD-0004).

The Darwin C17 object build uses a deterministic fixed-point measurement pipeline. A bootstrap
compile uses the pinned Apple Clang with
`-O0 -fstack-usage -fno-inline-functions -fno-lto` and
`R_STACK_USAGE_MEASUREMENT=1`. The build rejects missing or empty reports, malformed,
duplicate, or dynamic generated-source records, and any generated frame above the
target-manifest ceiling. It creates an ordered `R_STACK_FRAME_<function>` bounds header from
that report, pre-includes the header in a candidate compile, remeasures the candidate, and
updates each bound to the maximum of its previous value and the candidate frame. Candidate
compilation repeats until the header no longer changes, with the target-manifest iteration cap
enforced as a hard failure. Every iteration must retain the exact bootstrap generated-function
name set and static frame kinds. The linker consumes the exact candidate object compiled
against the converged header instead of recompiling generated C.
Once the frames are stable, `tools/compute_stack_entries.py` derives `R_STACK_ENTRY_<entry>` for
every entry as its frame plus the longest acyclic callee path through the generated call graph
(direct calls, type-glue callbacks and the indirect-call markers; another entry's own gate is
never nested), appends those defines to the same header and fails the build when a bound exceeds
the target manifest's `core.stack.entry_budget_bytes`.
`tests/check_codegen_program.cmake` is the current reference driver for this process.
Sanitizer and TSan builds keep the bootstrap and instrumented object phases but are explicitly
diagnostic-only:
instrumentation may create dynamic or larger frames, so those builds emit a nonconformance
marker and make no stack-conformance claim. Therefore `--emit=c17` alone is useful for
deterministic source inspection, but its output is not a standalone conforming object-build
command without the measured header.

The C17 emitter collects `constexpr str` literals from every declaration selected for the
application emission before writing function bodies. Exact post-escape byte sequences are
deduplicated across modules and across synchronous and asynchronous lowering, sorted by bytes and
length, and emitted once as file-scope `r_program_string_*` arrays. Descriptors retain their
explicit byte lengths; an empty value uses the single program-image sentinel.

Conditional expressions (`condition ? a : b`) lower to a typed HIR value and MIR
branches with a typed `phi`. Both C17 paths evaluate the condition once and execute
only the selected arm, including its calls and ownership transfers. Copy places are
read; named Move arms require `move`. Reachable ownership states join as for `if`.
Numeric widening, string views and shared-borrow conversions follow the existing
conversion rules. Borrowed results retain all possible origins, with component-level
mappings when those are complete. A conditional never discards a dependency or extends
the lifetime of its input storage.

`error Name { ... };` declares checked errors with either struct fields or enum variants.
The word is contextual and remains available in names such as `std.error::error`.
Ordinary structs and enums cannot appear in checked-error positions. Record and variant
errors, fieldless enums, and payload enums are lowered. Fieldless enums support explicit
constant discriminants and all grammar-admitted R/C integer representations. Payload
variants support positional and struct-like constructors, exhaustive sync/async switches,
active-payload transfer, and cleanup.
`move` on a named Copy value performs an ordinary read and leaves its source usable.
Standard categories are listed explicitly in `compiler/semantic/standard_errors.def`,
shared with the inventory generator; no suffix-based inference is used.

`mir` is a typed control-flow representation distinct from the source-shaped HIR. It
uses SSA value temporaries, explicit mutable places, deterministic function/value/block/
place ordinals, and explicit `load`, `store`, `branch`, `jump`, `phi`, `return`, and
`unreachable` instructions. `interface` contains exported signatures and generic schemas. Every exported
function carries its logical value type, canonically sorted exact error set, success/error tag
table, and carrier kind. Async records additionally separate the immediate start descriptor from
the task-completion descriptor. Its layout SHA-256 covers a domain separator, the canonical
descriptor bytes, and the exact target-manifest bytes; the record says whether that hash is bound
to a supplied target manifest. `link-plan` records the selected entry/profile, sorted module units,
required Standard Library module targets, and SHA-256 content fingerprints of optional target/link
manifests; host paths are not embedded. Required library targets are emitted in deterministic
Standard Library dependency order. `bundle` contains the interface, MIR, and link plan as named
deterministic sections.

MIR and link-plan remain schema version 1; the interface is schema version 21. Version 21
adds `held=(...)` to a borrow bound: the inputs an output takes only what their designated
storage holds from (R-BORROW-0009). Version 20 writes the value of an exported aggregate object
as the canonical term of its initializer. Version 19
writes a dyn interface as `(dyn "dyn(...)")` with its canonical contract (R-TYPE-0051). Version 18
adds `constants=(...)` to trait records, the name, type and default presence of each associated
constant (R-TYPE-0050); a bound over one is written as `(constant associated module trait name
owner)`. The interface became checked-effect-aware in version 14. Exported aggregate records retain their structural `kind` and include an explicit
`error=true/false` category, independent of `copy` and layout. Consumers must read version 14
to preserve checked-error eligibility and generic schema identity across module boundaries.
Version 14 adds per-component normal-result and checked-error borrow paths. Output paths use
field names and fixed indices; input paths identify a zero-based parameter followed by its
projections. A wildcard index retains the whole input bound. Incomplete mappings remain
conservative; the union bound is still an upper bound and does not establish disjointness.
The records contain no compiler-local symbol or HIR identities. Version 14 also carries
trait applications, associated bounds, supertraits and default-body fingerprints; opaque
result contracts and representation fingerprints; and the `scoped=true` function marker.
Version 13 adds resource-qualified and checked/async callable signatures, significant-result
contracts (`must_use=true` and `discardable=true`, each omitted when false), and `borrow_contract`
on functions and schemas.
Borrow bounds distinguish no borrows, static origins, exact zero-based input sets and conservative
symbolic input bounds. Each bound covers all nested projections as a union, including checked
errors independently of the normal result. It never grants disjointness or extends an owner lifetime.
Generic source definitions remain required; the record is not a serialized executable body.
The compiler preserves projected borrow state and error bounds when closing generic functions.
Version 12 adds typed constant size parameters and arguments.
Version 11 adds structural callable constraints with exact shared/mut/once modes and omits
synthetic callable traits from exported records. Version 10 adds independent `noalloc=true/false` and `nonblocking=true/false` declared
contracts to ordinary functions and generic function schemas (R-FUNC-0019). The
semantic pass follows R callees and destruction, uses explicit C-import contracts,
and rejects unknown runtime effects. Async launch allocation is checked separately
from the annotated body; current mutex-based executor paths do not prove nonblocking
execution. Neither annotation is inferred from `throws` or Copy.
Version 9 adds the canonical `overload="name(parameter-types)"` identity to every exported
overloaded function and generic function schema. Selection prefers a unique exact argument
match and otherwise requires a unique compatible candidate; result types do not select.
Standard receiver mappings are generated from `library/standard_methods.json`.
Version 8 added `variadic=true` to the function records of variadic functions (R-FUNC-0018).
Version 7 added `associated=(...)` to trait records, `bindings=(...)` to implementation
records, spells core traits as `core::Name` in implementation records, writes a projection
type as `(projection base "Name")`, and never exports the core traits themselves.
Version 6 added `(trait ...)` records with their method inventory and definition hash,
`(impl ...)` records naming the trait, the target type and the definition hash, and the
normalized trait constraints of every schema parameter; trait prototypes are not exported as
function schemas.
Field records include canonical JSON flags, renamed keys, case policy and default expressions.
Default factories export their qualified names and source-definition hashes. JSON hooks export
their independent directional links and hashes; json_encode/json_decode are normalized constraints.
Exported module objects record their static or thread-local storage and constant initializer.
Functions with borrowed results record the exact zero-based set of parameter origins, including
results that may originate from more than one parameter.
Aggregate JSON fingerprints hash canonical field types, contracts and transitive member schemas,
including enum variants, hooks and default factories. Nominal back references close recursive
schemas; type aliases preserve identity. Generic records include normalized constraints, dependent member schemas, hook links,
definition fingerprints and sorted source dependencies. For an open schema, `copy=true`
means its constraints prove Copy; otherwise Copy is determined from the closed fields
and drop hook. Each instance has concrete HIR/MIR
and C17 code. Definition sources are required for imported generic bodies. These outputs
are compiler-interface foundation artifacts, not the final
release-package format. The driver records target/link-manifest SHA-256 identity.
Before C17 emission, a supplied target manifest must match the pinned target identity exactly.
The C spellings, checked-conversion carrier classes, runtime range descriptors, and host ABI
assertions are generated from that manifest by `tools/generate_target_abi.py`; the normal build
uses the committed generated files and does not run Python.
For `std.c::link_available`, C17 emission additionally reads the link manifest's
`links[].logical_name` and optional `links[].available` resolved facts, validates logical
names, rejects duplicates, sorts them deterministically and emits an immutable lookup
view. Target capability validation, complete R-FFI manifest identity verification and
physical library resolution remain work for the target/link resolver.

`tests/bench/` holds R/C benchmark pairs (`baseline`, `call_overhead`, `index_fixed`,
`index_slice`, `checked_arith`, `baseline_async`, `async_task_start`, `async_scoped_start`,
`async_fs_read_into`, `array_push`, `format_int`, `own_alloc`, `array_get`, `string_append`,
`dict_lookup`, `json_parse`, `deflate_roundtrip`, `tcp_echo`).
The CMake target
`benchmarks` compiles each R program through the codegen pipeline at `-O2`, compiles the C
mirror with the same compiler and flags, times both and writes `docs/benchmarks.md`; the
ctest `r_bench_pairs_agree` only verifies that each pair returns the same checksum. The R
programs link the runtime and standard libraries of the configuration the target runs in,
so measurements are only accepted from an optimized configuration (`cmake -S . -B
build-release -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build-release --target
benchmarks`); a Debug tree runs the harness only in `--check` mode.

The deny-panic-allocation policy of R-OBJ-0012 is selected per module by writing
`@deny_panic_alloc` before the `module` declaration, or for the whole translation with
`--deny-panic-alloc`. Under the policy every `new`, `new arc` and `new rc` expression is
rejected with `R-DIAG-ALLOC-001`, as is any standard operation whose inventory record carries
`panics_on_allocation_failure` (none in Library 0.1 by R-LIB-0007; the generated operation
registry carries the flag for each entry). Fallible allocation through `std.alloc::try_new`
and the checked `throws std.alloc::alloc_error` operations is unaffected, and
`tools/audit_library_source_surface.py --deny-panic-alloc` proves that the whole closed
library surface still resolves under the policy. The attribute anywhere else is
`R-DIAG-SYN-002`.

`std.secret` (R-SLIB-SECRET-0001..0004) lowers to `library/std/secret`. `std.secret::buffer`
is a Move-only, Send, non-Sync owner whose drop glue erases the whole allocation, unused
capacity included, through volatile stores before release. `with_length` is a checked
`alloc_error` call through the hosted allocator, `from_bytes` adopts a consumed `bytes`
allocation without copying, `as_slice`/`as_slice_mut` return call-bounded views carrying the
buffer origin, and `zeroize`/`constant_time_equal` take call-bounded byte slices. The
synchronous path and the async frame path both emit these calls; a shared borrow of a buffer
is rejected where Send is required (`R-DIAG-ASYNC-001`).

With both `--module-map` and `--entry`, the driver treats the map as an indexed
`module = path` table: it loads the entry module, scans its imports, and follows only
reachable entries. Without `--entry`, it retains the all-module behavior used by parser
conformance runs. Duplicate keys, a map key that disagrees with the source `module`
declaration, and missing reachable imports are driver errors. `--library-map FILE` adds the
`MODULE = PATH [PROFILE]` table of standard modules written in R (`library/r/library.map`):
a reachable import of a listed `std.*` module that no module-map entry provides loads the
listed source as a library source, transitively, with the listed least profile; a program
module under a reserved root remains a driver error.

Exit status `0` means a clean requested emission. Status `1` means source
diagnostics, unresolved CST ambiguity, or a requested HIR/MIR/C17 emission that is
outside the implemented lowering slice. Status `2` means I/O, internal, or
resource failure.

## Generated inputs

The normal build consumes the committed `compiler/lexer/lexer_generated.c` and
does not require re2c. Regeneration requires exactly re2c 4.5.1. The
`verify_generated_lexer` CMake target regenerates to a temporary sibling and
performs a byte-for-byte comparison.

Unicode XID and NFC tables are generated from Unicode 17.0.0. Pinned versions and
the UCD archive digest are recorded in `tools/toolchain.lock`. The Annex A grammar
coverage manifest records the exact Annex hash and per-section production-name
hashes; `tools/check_grammar.py` fails when the normative EBNF changes without an
explicit frontend review.


User-defined generics use `@generic<T>` declarations and closed types such as
`Box<i32>`. Bodies are checked before instantiation, including unused definitions.
One `@generic<...>` header may appear among the ordinary attributes, before declaration
modifiers. It applies to a struct, enum, error, function or associated hook. The former
`generic(...)` declaration header is rejected with `R-DIAG-SYN-001` and
`use @generic<...> to declare generic parameters`; `generic<...>` without `@` is treated
the same way, and `@generic(...)` reports `generic parameters are written @generic<...>`.
Ordinary identifiers and applied types named `generic`, such as `generic<i32>`, remain valid.
The semantic instantiation cache substitutes checked types and HIR; it never edits
or reparses substituted source text. Names use canonical type arguments and source
module identities. See [the executable Vector example](../examples/generics/README.md).
Dependent array bounds retain checked constant expression DAGs until their type
arguments are closed. Native layout facts come from pinned scalar target metadata
and the same standard/runtime ABI headers used by generated C17. Layout and bound
arithmetic are checked before executable array types are emitted.

The generated predefined core key helpers use the `r-core-key-v1` mapping: integers,
booleans, characters and enum values convert to `u64`; strings use FNV-1a-64 over
exact UTF-8 bytes; floating values use binary64 bits after canonicalizing zero and
NaN; pointer families use their allocation identity and offset (native pointers,
owner allocations, or shared-owner control blocks). Non-pointer mappings are stable
across executions on the pinned target. Pointer hashes are execution-local.
Dictionary seed mixing is separate. These helpers do not read object padding.
