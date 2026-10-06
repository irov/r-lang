# Working in the R repository

This file is for coding agents (Claude Code, Codex and others) and for people who want the same
rules. It says what the repository is, which gates a change must pass, and what is easy to get
wrong. Longer references: [compiler/README.md](compiler/README.md) (the compiler, its artifacts,
build and test), [docs/C_CODE_STYLE.md](docs/C_CODE_STYLE.md) (the C contract),
[examples/README.md](examples/README.md) (the application catalogue),
[docs/language-completeness-roadmap.ru.md](docs/language-completeness-roadmap.ru.md) and
[docs/language-completeness-matrix.ru.md](docs/language-completeness-matrix.ru.md) (the stage
history, in Russian).

## What this is

R is a systems language with ownership, checked errors and structured concurrency. The
repository holds everything of its 0.1 implementation for one target, `arm64-apple-darwin`:

| Directory | Contents |
| --- | --- |
| `specification/` | The normative Core (`R_LANGUAGE_SPECIFICATION_0_1`) and Standard Library (`R_STANDARD_LIBRARY_SPECIFICATION_0_1`) specifications, English and Russian, AsciiDoc (Core also rendered to Markdown); `generated/` holds the rule inventories derived from them |
| `compiler/` | `r-front`: re2c lexer, CST, AST, whole-program semantic analysis, typed HIR and MIR, strict ISO C17 emitter. Strict C17, no stable external ABI |
| `runtime/` | The hosted runtime (allocator, containers, strings, tasks) and `runtime/darwin` (executor, payload I/O, sockets, processes, timers, file-system lane) |
| `library/` | The standard library: `core` and `std/*` in C (one public operation per `.c` file), `r/std/*.r` modules written in R and listed in `library/r/library.map`, `native/*` providers behind the checked FFI (TLS over Mbed TLS, crypto over libsodium and Mbed TLS, SQLite, mmap), `internal/` shared code, `generated/` inventories |
| `targets/` | The pinned target manifests (toolchain, ABI, stack budgets, specification revisions) |
| `tests/` | C unit tests, CMake check drivers, R fixtures, golden artifacts, Python differential tests and the `run_*_examples.py` drivers of the examples |
| `examples/` | The example applications, each with `src/*.r`, `modules.map` and a README; `catalogue.cmake`, `coverage.json` and `syntax-coverage.json` index them |
| `tools/` | Generators and checkers (rule inventories, library inventory, registries, ABI digests, style, parity, coverage); `toolchain.lock` pins the reference tools |
| `cmake/` | `RThirdParty.cmake`: download of pinned third-party archives at configuration |
| `docs/` | Design and audit documents; the roadmap and matrix record every stage and its verification |

The specification is normative. The implementation follows it, not the other way round: a
behaviour that is not specified is first written into the specification (both languages), then
implemented, then verified. Every rule has an identifier (`R-FUNC-0020`, `R-SLIB-PG-0005`, ...)
and comments, tests and documents cite those identifiers.

## Rules that are not negotiable

1. **No commit, amend or push without the owner's explicit request in the conversation.** Finish
   the work in the tree, run the gates, report what is uncommitted.
2. **No third-party files in the repository.** External sources and data are downloaded at
   configuration by `r_third_party_archive` (`cmake/RThirdParty.cmake`) with a pinned URL and
   hash into `build/third_party`, which all build trees share. Our own generated tables and
   extractions (Unicode tables, RFC examples) stay in the tree. The one derived file is
   `compiler/codegen/layout.inc`, a C translation of parts of clang-format: it keeps its
   SPDX header and the LLVM license text in `compiler/codegen/LICENSE-LLVM.txt`; code taken or
   translated from elsewhere is declared the same way, never disguised.
3. **Generated files are regenerated, never edited by hand.** Each has a generator in `tools/` or a
   `regenerate*` CMake target, and a `--verify`/`--check` mode or a test that fails when the
   committed file is stale.
4. **Every warning is an error; no suppressions.** Project C is strict ISO C17 with the warning set
   of the CMake files. Third-party code (`r_sqlite3`) is the only documented exception.
5. **No `assert` in production or generated C** (`production-assertion-check`). Contract
   violations panic through the runtime (`r_runtime_panic`) or abort; tests use their own
   `R_TEST_CHECK`/`require` helpers.
6. **English and Russian specifications move together**, with the same rules, the same tables and
   the same line structure of tables (`tools/check_spec_parity.py`). A revision bump touches all
   the places listed below in one change.
7. **Nothing is "done" before its gates pass** (see *Gates*), and the stage record in the matrix
   says what was run and what failed.

## Environment

macOS on Apple silicon with the Xcode command-line tools pinned by `targets/*.json` (the build
checks the compiler and SDK: `target-toolchain-check`), CMake ≥ 3.25, Python 3, and from
Homebrew: `mbedtls@3` (std.tls, std.crypto), `libsodium` (std.crypto), optionally `postgresql@17` (the
std.postgres tests and the `orders` example register only when `pg_ctl` is found). The build
downloads the SQLite amalgamation and the COSE examples on first configuration; a machine
without network needs the archives copied into `build/third_party` (the error message names the
file and hash). Optional for their checks only: `clang-format 22.1.8` (`format-check`), `re2c
4.5.1` (`verify_generated_lexer`), `asciidoctor 2.0.26` (`specification-render-check`); the
exact versions are in `tools/toolchain.lock`. Homebrew's `llvm@22` carries clang-format 22.1.8;
link only that binary into `PATH` (`ln -s /opt/homebrew/opt/llvm@22/bin/clang-format
/opt/homebrew/bin/clang-format`), since the rest of that LLVM would shadow the pinned Apple clang.

## Build and test

Four CMake presets, each with its own tree under `build/`:

```sh
cmake --preset debug && cmake --build --preset debug -j8 && ctest --preset debug -j6
cmake --preset sanitizers        # ASan + UBSan
cmake --preset thread-sanitizer  # TSan
cmake --preset fuzz              # libFuzzer if available, else a standalone driver; ASan/UBSan
```

- Run a subset with `ctest --test-dir build/debug -R 'pattern' --output-on-failure`. Test names:
  `r_frontend_codegen_<fixture>` compiles and runs `tests/fixtures/codegen_<fixture>.r`;
  `r_frontend_codegen_example_<name>` builds an example and `r_example_<name>_commands` runs its
  driver; `r_frontend_codegen_rtest_<module>` translates `library/r/tests/<module>.r` in test mode
  and runs it; `r_library_*_unit` are C unit tests; `r_*_vectors` and `r_*_differential` compare
  with an oracle (Python modules, RFC vectors, psql).
- A full run of one preset takes 8–30 minutes and writes a few GB of `codegen_*` artifacts into
  `build/<preset>/tests`; delete them between runs when the disk is tight. Pass
  `--output-on-failure`, otherwise the cause of a rare failure is lost when `LastTest.log` is
  overwritten.
- Never edit sources or fixtures while `ctest` runs in the same tree: fixtures and R modules are
  read at test time.
- The complete verification of a stage ("the battery") is all four presets plus the audits below.
  Four long command tests (`calculator`, `numbers`, `deflate`, `xml`) and `r_regex_differential`
  are skipped under TSan for time; everything else runs everywhere.
- A failure that passes on rerun is not a flake until proven: of the last six "flakes" five were
  real races (lost bytes, use-after-free, cancellation windows). Loop the test under CPU load
  (`yes > /dev/null` hogs, 4–12 parallel copies of the binary) before calling it timing.
- After reverting a temporary mutation by copying a file back, `touch` it: make compares
  modification times at one-second resolution and can keep the mutant's object file.
  Python has the same blind spot: a tool imported and then edited within the same second to a
  file of the same size keeps its stale `__pycache__` bytecode; delete the `.pyc` after such an
  edit (a digest replaced by a digest is exactly that edit).

## Gates

Build these targets from `build/debug` and run the two audit scripts before reporting a change
as finished. The matrix records their results per stage.

| Target or command | Checks |
| --- | --- |
| `specification-check` | standard methods table, EN/RU parity, grammar manifest, rule inventories, target manifests, target ABI digests, Copy/Move ABI, type and operation registries — all against the committed files |
| `c-style-check` | the invariants of `docs/C_CODE_STYLE.md` that clang-format cannot express |
| `production-assertion-check` | no runtime assertions in production or generated C |
| `library-source-surface-audit` | every concrete public library name compiles from R source |
| `runtime-entry-stack-inventory-check` | the runtime entry stack inventory matches the measured entries |
| `library-layout-check`, `library-coverage-check`, `verify_library_inventory` | library source ownership, inventory coverage and freshness (also run as tests) |
| `semantic-slice-audit`, `lowering-rejection-audit` | informational (exit 2 by design): counts of semantic slice sites and lowering rejections; compare with the previous stage |
| `python3 tools/audit_compiler_implementation.py --compiler build/debug/r-front` | the implementation probes (all must be accepted) |
| `python3 tools/audit_language_safety.py --compiler build/debug/r-front --sanitized-compiler build/sanitizers/r-front` | the safety probes (all must pass) |
| `format-check` | clang-format conformance (needs the pinned clang-format) |

## Checklists by kind of change

### Compiler (`compiler/`)

- Tests live in `tests/*_tests.c` (unit), `tests/fixtures/*.r` with `tests/check_*.cmake` and
  `tests/check_*.py` drivers (acceptance and rejection with exact diagnostics), and
  `tests/golden/*.sexp` (artifact goldens). A new diagnostic needs a rejection fixture; a new
  lowering needs a sync and an async fixture (`codegen_<x>.r`, `codegen_async_<x>.r`).
- Diagnostics store message pointers: pass string literals, never stack buffers.
- Compiler-interface artifacts (`--emit=interface`, HIR/MIR dumps) carry an interface schema
  number. Changing their shape bumps it in `compiler/mir/mir.c`, the `tests/check_*.py` that
  assert it, `tests/compiler_interface_tests.c`, every "Interface schema N" sentence of both
  specifications and `compiler/README.md`.
- New enum values of standard operations go at the end of their enum; dump switches in
  `compiler/hir/hir.c` and `compiler/mir/mir.c` need the new cases.
- Describe the design in the relevant section of `compiler/README.md`; it is the compiler's
  reference, not a changelog.
- clang-format sorts the `#include` lines of one block; fragments (`.inc`) that depend on the
  ones before them stand in their own blocks, separated by a blank line.
- The generated C must be a fixed point of clang-format 22.1.8: with it installed, every codegen
  test has a `_format` twin. Statements are laid out by `layout.inc`, file-scope initializers and
  declarations by the measured rules of `layout_pass.inc` (described in `compiler/README.md`);
  a comment line stays glued to the line after it in that pass, so put emitted comments before
  a condition, not before a statement that may need breaking.
- Optimizations that remove checks or tasks (`index_proofs.inc`, `loop_versions.inc`,
  `direct_calls.inc`) are proven by marked fixtures (`/* proven */`, `/* versioned */`,
  `/* direct */` and their opposites) checked by `tests/check_*.py` against the generated C,
  plus a run of the same fixture; a test wrapper that hooks `r_runtime_task_execution_await`
  sees no await of a body that runs as a direct call and must disable direct calls.

### Specification (`specification/`)

- Edit the English and Russian AsciiDoc together (and the Core Markdown renders), keeping rule
  identifiers, table rows and the line structure of table rows identical.
- Regenerate the rule inventories: `python3 tools/generate_rule_inventory.py --specification
  specification/<DOC>.en.adoc --inventory specification/generated/<DOC>.rules.json --write` for
  the changed document, then `specification-check`.
- A revision bump (`0.1.0-draft.N`) and a rule-count change touch:
  `tools/generate_rule_inventory.py`,
  `tools/check_grammar.py` (Core), `tools/check_target_manifest.py` (both counts, three places),
  `tests/tooling/test_spec_contracts.py`, `targets/*.json`, `compiler/include/r_frontend.h`
  (Core), `compiler/grammar/annex_a_manifest.json` (Core), the revision rows of the specification
  files, `tests/golden/*.sexp` (`core_revision`), `compiler/README.md`; then regenerate the
  grammar manifest and the target ABI digests (`regenerate-grammar-manifest`,
  `regenerate-target-abi`) and rebuild `r-front` (the digests are compiled in).
- Annex A of the Library specification lists every public item; a new operation or type needs
  its row, and the library inventory (below) is derived from the English document.

### Standard library (`library/`)

- C modules: one public operation per `.c` file named after it, one non-`static` symbol; shared
  code in `library/internal/<subsystem>`; `r_library_internal_*` names never appear in generated
  interfaces. Native providers (`library/native/*`) are reached only through the checked FFI
  declared in `library/r/links.json`.
- R-source modules (`library/r/std/*.r`): register in `library/r/library.map` with the least
  profile that provides the module; a module of the `allocation` or `freestanding` profile cannot
  use `std.string`, `bytes`, f-strings or `@derive(format)` (gate hosted items with a module-level
  `@if` on `core::profile`). `tests/check_library_profiles.py` (test `r_library_source_profiles`)
  verifies the profiles. The R part of a C module is a map entry under the C module's path.
- After any public-surface change, regenerate in this order: `regenerate_library_inventory`,
  `python3 tools/generate_standard_methods.py`, `regenerate-standard-operation-registry`,
  `regenerate-standard-type-registry`, `regenerate-named-standard-copy-abi`,
  `regenerate-named-standard-move-abi`, `regenerate-target-abi`, then rebuild. Module counts are
  asserted in `tests/tooling/test_library_inventory.py`.
- Every public inventory record must be used by an example application
  (`tools/check_example_coverage.py --frontend build/debug/r-front --write` regenerates
  `examples/coverage.json`; the `r_example_catalogue` test fails when it is stale, also after a
  mere edit of an example's `.r` file).
- Tests of R-source modules are `library/r/tests/<module>.r` with `@test` functions
  (`std.test`); the file is discovered by the build. Tests that need external services skip
  cleanly without the driver's environment (see `tests/run_postgres_tests.py`).
- A new runtime entry point: `tools/generate_runtime_entry_stack.py`,
  `regenerate-runtime-entry-stack-inventory` and the count in
  `tests/tooling/test_runtime_entry_stack.py`.

### Examples (`examples/`)

- An application has `src/*.r`, `modules.map`, a README with its commands and expected output,
  an entry in `examples/catalogue.cmake` and `catalogue.json`, a row in `examples/README.md`, a
  driver `tests/run_<name>_examples.py` registered in `tests/CMakeLists.txt`, and regenerated
  `coverage.json` / `syntax-coverage.json` (`tools/check_example_syntax.py`).
- Drivers compare exact stdout, stderr and exit status. Environment failures are not caught in
  examples (they end as `std.error::fault`, exit 71); CLI-contract failures use `sysexits`-style
  codes (64 usage, 65 data, 69 unavailable).

### Runtime (`runtime/`)

- Darwin adapters publish one linearized terminal outcome per operation (R-SLIB-ASYNC-0007):
  task cancellation is selected immediately but reaches the native request asynchronously, so an
  operation with a commit point either gates its native entry (the
  `r_runtime_darwin_io_prepared_set_shutdown_entry` pattern) or forces completion after a
  committed native call (`r_runtime_task_external_select_terminal_completion`). Late callbacks
  (cancel, deadline, close) must hold a reference to the storage they touch.
  `r_runtime_task_external_try_select_completion` never replaces a selected cancellation, so a
  cancel callback may acknowledge a waiter it withdrew; only `..._at` with a sequence captured
  at the native event may replace one, and its adapter must then not acknowledge twice (P4.1-8).
- Hosted programs unwind panics by explicit propagation (L39, compiler/README.md *Panic unwind*):
  `r_runtime_raise` begins a panic and returns, and generated code tests `r_runtime_unwinding()`
  after calls, standard operations and drops that may run R code. `r_runtime_panic` in runtime or
  library C still aborts (R-ERR-0006), so C code reports conditions R code must observe as a
  status, or raises and returns when only generated code calls it (`once_poisoned`, the
  escalation of a scoped thread). C code that calls back into R (type glue, initializers, pool
  and thread entries) must expect the callee to return with a panic pending: test
  `r_runtime_unwinding()` before reading what the callee wrote, and never run R code of another
  task while one is pending. An unobserved report goes first to the panic sink
  (`r_runtime_panic_set_sink`, used by the `std.log.native` provider for
  `std.log::panic_reports`) and becomes the default stderr line only when the sink does not take
  it; the sink runs on the delivering thread and must neither block nor run R code.
- Testing hooks (`R_RUNTIME_DARWIN_IO_TESTING`, `R_RUNTIME_DARWIN_FS_LANE_TESTING`) pause a worker
  at a chosen point so a race becomes a deterministic test; prefer them to sleeps. New one-shot
  pauses use `RRuntimeDarwinIoTestPause` (`io_internal.h`). An unarmed hook costs one atomic load:
  the Release tree with tests, where `benchmarks` runs, compiles the hooks in.
- Payload I/O has three engines. Regular files go through the file payload adapter
  (`io_direct.c`, R-SLIB-ASYNC-0019): at most `R_RUNTIME_DARWIN_IO_FILE_MAX_ENTERED_TRANSFERS`
  transfers are in `pread`/`pwrite` at once, a bound the target manifest and
  `tools/check_target_manifest.py` pin. Stream sockets use nonblocking calls and per-handle
  readiness sources. Dispatch I/O remains for the console, process pipes and the FIFOs, terminals
  and devices that `std.fs` opens (the engine follows `fstat`). A completion registered
  with `r_runtime_darwin_io_request_set_completion_inline` may run on the thread that finishes the
  request, or before the registration returns.

## Writing R in the library and examples

The library modules and examples are R programs; the compiler enforces the specification, and
these rules are the ones that most often reject otherwise reasonable code:

- Grammar: `if (c) { } else { }` only — there is no `else if`; use `switch`, early `return` or
  nested blocks. `throw (condition) error_value;` is the conditional throw.
- Reserved words that look like names: `list`, `raw`, `import`, `export`, `move`, `drop`, `task`,
  `module`.
- R-NAME-0009: a parameter or local may not reuse a component of an imported module path or the
  name of a module function (`server` in a program with `example.arena.server`, `failure` where
  the module declares `failure`).
- R-FUNC-0024: a call does not continue after a method call unless the method is `@chain`;
  parenthesize the receiver, as in `(lease.get())->query(...)`.
- Functions are declared before use in source order; `protected` marks module-private items.
- R-FUNC-0020: every result is used, forwarded or discarded with `as void` on every path, including
  the zero-iteration path of a loop; a named Move value is discarded with `drop name;`.
- R-INIT-0014: a value produced inside `task_scope` is accumulated into storage declared outside
  it (`total += await child;`), not assigned.
- `@scoped` async calls need an enclosing `task_scope`, even inside `@scoped` functions, and a
  scoped child may borrow only storage declared outside that scope (nest a second scope for
  values created inside).
- A borrow local (`guard.get_mut()`) cannot live across `await` in a non-scoped async function;
  pass it inline.
- `new arc T(v)` directly as the argument of an awaited call is outside the C17 lowering; bind it
  to a local first.
- Importing an R-source module requires `import std.x;` even for the R part of a C module.
- `constexpr str` converts to `const u8[]` through a `str` local (one conversion per expression).
- A `std.string::string` place is viewed as `str` wherever `str` is expected
  (`db.execute(sql, params)`, `str name = record.name;`, `*text` for a pointer); a temporary
  string (a call result, an f-string) needs a local first, and a string is never a byte slice
  (`as_bytes()`). Write `.as_str()` only where no `str` is expected (`switch`, generic
  arguments).
- An attribute of the program is a struct marked `@attribute(type|field|variant)`; use it as
  `@name(arguments)` after importing it by name (`import m::{key};`) or as `@m::key`, and read it
  with `core::type_attribute::<A, T>()`, `core::field_attribute::<A, T>(index)` or
  `core::variant_attribute::<A, T>(value)`. Arguments are literals or `Enum::name`; `variant` is
  a keyword, so a variant target is written `@attribute(variant)` only.
- `core::location()` is the translation-time `module.path:line` of the call; pass it explicitly
  (`logger.log_at(level, message, &fields, core::location())`), since R has no implicit caller
  parameters.
- A panic ends its task and `await` continues it in the awaiting task; to survive the panic of a
  child (a request handler), await it as `await std.async::join(move t)`, which gives a
  `std.thread::join_result<T>` and admits only a task without checked errors (catch them inside).
- R-FUNC-0012: a task with checked errors must be awaited, cancelled or detached on every path,
  throws included, so a member that lives across a whole loop returns its failure as a value
  instead of throwing it (`watch_stop` in `std.service`).
- `u8` and `u16` operands of arithmetic, bitwise operators and shifts promote to `i32`, as in C;
  narrow the result back with `as` (`(entry >> 4usize) as u16`).
- Hot loops: the compiler leaves out a bounds check it can prove (compiler/README.md, *Index
  and conversion proofs*). Index a local slice or `array<T>` (one the body never borrows
  exclusively) with a local that a condition bounds (`i < len(b)`, or `n == len(b)` and
  `i < n`); take a sub-slice `b[lo..hi]` once and index inside it (`b[a..a + 16]` holds 16);
  mask by a constant, or by a local checked once (`if (mask >= len(t)) { return; }`, then
  `t[x & mask]`). An index through a field (`this->table[i]`) or with an offset (`b[i + 1]`)
  keeps its check. `tests/fixtures/codegen_index_proofs.r` lists proven and checked forms.

One rule is not a compile error but has cost real defects (P4.1-5): a member that a `select` did
not choose, or a wait that lost to an until clause, may already have taken its value, and
`cancel_all` destroys that value with the unobserved result (Core R-STMT-0018). A receive, a
notification wait or a signal wait raced anew each round can therefore lose what it took. Take
values that must not be lost without waiting (`try_recv`) after a wake-up whose loss is harmless,
or keep one wait pending for the whole loop and test it with `first`/`first_until`.

## Documentation and records

- `docs/language-completeness-roadmap.ru.md` lists the stages (L = language, M = modules); each
  stage has a checklist, a "done when" criterion and a completion note. The matrix
  (`docs/language-completeness-matrix.ru.md`) has one section per stage with the item table, the
  design decisions, every defect found (`<STAGE>-<n>`, cause and fix) and the final verification
  table (all presets, audits). Write them in Russian, in the same register as the existing
  sections; record a decision the roadmap leaves open as yours, with the reason.
- Code comments, READMEs, commit messages and `compiler/README.md` are English. Comments explain
  why and cite rule identifiers; they do not restate the code.
- Commit messages: a short English subject, a body that says what changed and why, with the
  specification revisions when they moved.

## Housekeeping

- Keep the repository root to the files already there; scratch files, logs and downloads belong
  outside the repository or under `build/` (ignored).
- `.gitignore` excludes `build/`, `*.plist` (static-analyzer reports), Python caches, editor and
  assistant settings and `.DS_Store`. Do not add exceptions for local tools.
- Test clusters (PostgreSQL) and servers started by drivers are stopped by the drivers; a killed
  driver may leave one — check with `ps` before and after long runs, and never stop processes of
  other projects on the machine.
