# R-CONF-G009 conformance matrix (arm64-apple-darwin)

R-CONF-G009 requires an implementation that claims external C library support to test a
real minimal C17 static library. The library is `tests/fixtures/ffi/probe_library.{h,c}`,
compiled with warnings as errors by every test that links it, and its link manifest is
`tests/fixtures/codegen_ffi_link_manifest.json`. Every row names the fixture or ctest that
covers the requirement on 11 September 2026; rows marked "not applicable" state why the
target does not exercise them.

| R-CONF-G009 item | Coverage |
| --- | --- |
| Function | `codegen_ffi_import`, `codegen_async_ffi_import` |
| Global | `codegen_ffi_types` (`probe_counter`, `const probe_version`, `thread_local probe_thread_slot`), `ffi_object_kind_mismatch`, `ffi_verify_object_mismatch` |
| Opaque handle | `codegen_ffi_types` (`struct` tag and typedef through the bridge), `ffi_opaque_by_value`, `ffi_opaque_deref`, `ffi_verify_opaque_mismatch` |
| Complete repr(C) struct | `codegen_ffi_records` (tag struct, typedef struct by value through the bridge, struct object) |
| Hidden-member-in-padding mismatch | `r_frontend_abi_record_ffi_record_hidden_member`: `struct probe_padded` hides `char hidden` inside padding; sizes and offsets agree, the inventory count does not |
| C integer constant | `codegen_ffi_types` (`PROBE_OK`, negative `PROBE_NEGATIVE`, unsigned `PROBE_LIMIT`, enumerator `PROBE_STATUS_READY`) |
| Same-value/wrong-type constant rejection | `ffi_verify_constant_type_mismatch` (`PROBE_LIMIT` declared `c_ulong`), `ffi_verify_constant_mismatch` |
| Valid promoted variadic call | `codegen_ffi_types`, `codegen_async_ffi_types` (`probe_sum`) |
| Unpromoted variadic argument rejection | `ffi_variadic_bad_argument` (`c_short`) |
| Variadic definition rejection | `ffi_variadic_definition` (R `extern "C"` definition with `...`) |
| Retained userdata | `codegen_ffi_userdata`: create/retain/release adapters around `std.arc::into_raw`/`from_raw` (R-FFI-0060, R-CMAP-0037); the C side holds two obligations and releases both |
| Portable null-for-non-null ingress | `codegen_ffi_null_ingress` (C passes NULL to an exported callback taking `raw c_int*`; the entry trampoline panics with `contract_violation`) |
| Invalid-enum ingress | `codegen_ffi_enum_ingress` (C passes 7 to a callback taking the verified `enum probe_mode`; the entry trampoline panics with `contract_violation`) |
| Omitted extra C enumerator | `r_frontend_abi_record_ffi_record_enum_missing` |
| Unused but representable enum value | `codegen_ffi_records` (`PROBE_MODE_IDLE` is declared and never used) |
| Missing artifact | `ffi_import_unavailable` (`probe-optional` is `available: false` for the target) |
| Missing typed symbol | `ffi_import_missing_symbol`, `ffi_import_data_symbol`, `ffi_object_kind_mismatch` |
| Header/ABI-record disagreement | `ffi_verify_record_claim` (record claims a `long` member; the verifier's layout probes fail against the header) |
| Wrong-provider evidence root | `r_frontend_abi_record_ffi_record_wrong_provider` |
| Record target/options/artifact mismatch | `r_frontend_abi_record_ffi_record_wrong_target`, `r_frontend_abi_record_ffi_record_wrong_options`; the artifact identity is the provider row above |
| Record compiler identity and header digests (R-FFI-0044) | `r_frontend_abi_record_ffi_record_wrong_compiler` (identity disagrees with the manifest toolchain), `r_frontend_abi_record_ffi_record_stale_header` (header digest disagrees with the file under `--abi-header-dir`), `r_frontend_abi_record_interface_fingerprint` (record and header digests in `--emit=interface`) |
| Two-provider same-spelling collision | `ffi_two_provider_collision` (`probe_increment` from `probe` and `system.libc`) |
| Cross-target host-fallback rejection | `ffi_import_unavailable`: an artifact unresolved for the target is rejected instead of falling back to a host library |
| Mutable save/canonicalize/restore floating environment (R-IDB-020) | `codegen_ffi_conformance`: an outbound call that sets `FE_UPWARD` and raises `FE_DIVBYZERO` leaves the next call observing the canonical mode and clear flags; a C-origin entry made under `FE_UPWARD` observes the canonical mode inside R and C observes `FE_UPWARD` restored after the entry returns |
| Proven immutable-canonical mode | not applicable: the target manifest declares `verified-runtime-helper` |
| Unavailable contract-only mode | not applicable: the target manifest declares `verified-runtime-helper`; `@fenv("preserve")` imports are accepted by `codegen_ffi_import` |
| Synchronous nested same-thread re-entry | `codegen_ffi_conformance` (`probe_outer` re-enters C through `probe_apply(probe_leaf, ...)` while attached) |
| Dynamic/framework/system | `codegen_ffi_bridge` (`system.libc`, kind `system`); dynamic and framework kinds share the darwin linker path and are exercised by the link-plan tests |
| Runtime-load failure | not applicable: no `dlopen`-style runtime loading is supported on this stage |
| TLS | `codegen_ffi_types`, `codegen_async_ffi_types` (`thread_local probe_thread_slot`) |
| Entry from a newly C-created thread | `codegen_ffi_conformance` (`probe_call_on_thread` runs the exported callback on a `pthread`; the entry attaches and detaches the thread) |
| Unsupported link kind | not applicable: every manifest kind is supported on the target; an invalid kind spelling is `ffi_import_bad_kind` |
| `c_wint` present | `codegen_ffi_conformance` (`probe_wint_echo` and the `WEOF` constant as `c_wint`) |
| All C compiles use warnings as errors | `tests/check_codegen_program.cmake`, `tests/check_abi_verifier.cmake` (`-Wall -Wextra -Werror -pedantic-errors ...`) |
| Sanitizer/substitute/non-applicability (R-CMAP-0015) | the FFI subset also runs under AddressSanitizer and UndefinedBehaviorSanitizer in `build-sanitize` |
