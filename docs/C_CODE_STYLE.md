# R C17 Code Style

This document is the required source and formatting contract for handwritten C in
`compiler/`, `runtime/`, `library/`, and `tests/`. Generated files follow their generator's
canonical format and are verified byte-for-byte instead of being manually formatted.

## Language and tools

- Project C is strict ISO C17. Target-specific runtime adapters are separate translation units
  and may use only the extensions recorded by the target manifest.
- Every compiler warning is an error. Warning suppressions in project code are not accepted;
  fix the cause. A third-party or generated boundary may carry a narrowly documented exception.
- The reference formatter is `clang-format 22.1.8`, pinned by `tools/toolchain.lock`.
- `format-check` is non-mutating and requires that exact formatter version. `format` performs the
  corresponding explicit rewrite. An ordinary build does not require clang-format.
- `.clang-format-ignore` excludes CMake inputs from direct formatter invocations; only `.c` and
  `.h` files belong to the clang-format contract.
- Source files are UTF-8 with LF line endings, a final newline, no tabs, no trailing whitespace,
  and no merge-conflict markers.

## Layout

- Use four spaces per indentation level and a 100-column limit.
- Use attached braces. Braces are required for every `if`, `else`, `switch`, `for`, `while`, and
  `do` body, including a body containing one statement.
- Do not place a complete function or control-statement body on one line.
- Associate `*` with the declarator: `const char *text`, not `const char* text`.
- A source file includes its own header first, then project headers, then system headers. Separate
  those groups with one blank line and sort each group lexically.
- Public declarations belong in headers. Every non-`static` function must have a visible prototype
  before its definition. Internal helpers that do not cross a translation-unit boundary are
  `static`.
- Forward `goto` is permitted only to one final label named `cleanup`. It is used solely to release
  partially acquired resources. Loops, retry logic, ordinary branches, and backward jumps use
  structured control flow.

## Names

| Entity | Form | Example |
| --- | --- | --- |
| Types | `R` + subsystem words in PascalCase | `RRuntimeTask`, `RStdArray` |
| Functions | lower snake case with `r_` prefix | `r_runtime_task_start` |
| Constants, enum values, macros | upper snake case with `R_` prefix | `R_RUNTIME_TASK_READY` |
| Fields, parameters, locals | lower snake case | `source_length` |
| C and header files | lower snake case | `path_from_utf8.c` |
| Header guards | `R_<SUBSYSTEM>_<FILE>_H` | `R_STD_FS_PATH_H` |

Names beginning with `r_library_internal_` are private implementation ABI. They are not R Standard
Library operations and must not appear in generated application interfaces.

## Standard Library translation units

- Each `core` or `std.*` module has its own directory and static-library target.
- One public R operation has one `.c` file and exactly one non-`static` public C operation symbol.
  The file name is the operation's lower-snake-case name.
- A generic operation has one type-erased implementation file. Generated code calls it with the
  type information of the concrete types; handwritten monomorphic copies are not accepted.
- Helpers used by only one operation are `static` in that operation's file. Helpers used by two or
  more operations live under `library/internal/<subsystem>`.
- Common code must not define public `r_core_*` or `r_std_*` operation symbols.
- `module.c`, `all.c`, amalgamation units, and unrelated public operations in one `.c` are not
  accepted.
- An intrinsic, generated specialization, compile-time-only item, or true ABI alias has no invented
  stub. It is instead recorded explicitly in the machine-readable API inventory.

`library-layout-check` enforces module ownership, inventory coverage, source-to-operation mapping,
one public symbol per operation file, CMake source membership, and the internal/public boundary.
The stricter release-only `library-coverage-check` applies the same validation and additionally
fails while any normative public item is still recorded as `unimplemented`.

## Ownership, failures, and safety

- Headers state whether each pointer or R value is borrowed, owned, or consumed and whether
  consumption occurs at call entry or only after a successful commit. A concise `Ownership:`
  paragraph immediately above the declaration is the canonical form.
- Production allocation goes through its explicit allocator subsystem. Direct `malloc`, `calloc`,
  `realloc`, or `free` is confined to `runtime/source/allocator.c`, the frontend's default allocator
  provider, and the developer CLI allocator provider. Native adapters receive an allocator and do
  not bypass it. Test-harness allocator callbacks may call the host allocator to inject failures.
- Validate external and runtime-dependent input with ordinary error handling. This includes bounds,
  overflow, allocation, encoding, operating-system, cancellation, deadline, closed-state, and race
  outcomes required by the public contract.
- Production compiler, runtime and Standard Library C, and the C translation units the compiler
  generates, contain no runtime assertion facility and do not use the C library `assert` macro.
  Compiler-proven preconditions are emitted as direct operations rather than repeated at runtime.
  Do not replace removed checks with `__builtin_assume`, `__builtin_unreachable`, or deliberate
  undefined behavior.
- Test compiler-proven preconditions and internal state-machine invariants through semantic negative
  tests, property tests, allocation/start-failure sweeps, fuzzing, sanitizer configurations, and
  concurrency stress. Compile-time `_Static_assert` remains required for ABI, layout, and other
  translation-time invariants because it emits no runtime code.
- When a nullable handle is the documented zero or moved-from representation, its drop or release
  helper keeps the `NULL` no-op branch; that branch is value semantics rather than defensive
  validation.
- Check sizes, integer conversions, alignments, and arithmetic before performing an operation that
  could overflow, truncate, misalign, or invoke C undefined behavior.
- Cleanup is exactly once and in reverse acquisition order. Initialize resource state before the
  first failing operation so the final `cleanup` block is valid on every incoming edge.
- Compiler code contains no mutable global state. Runtime process-wide state is opaque and every
  access follows its documented synchronization contract.

## Generated sources

The following families are excluded from handwritten formatting:

- the committed re2c lexer output;
- generated Unicode tables;
- generated API inventories and ABI records;
- the C ABI bridge and verifier translation units that `r-front` writes for C imports
  (`--emit=c17-bridge`, `--emit=abi-verifier`).

Their generators emit deterministic canonical text. Regeneration verification, golden tests, and
strict C17 compilation provide their formatting and integrity gate.
