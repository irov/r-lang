# Issues found while writing applications

This list records current limitations, not supported-language examples or expected
successes. A source-reference coverage percentage must not be used to hide them.

No limitation is currently open. The last three were resolved on 26 September 2026 (L14.5):

- `std.process::exit` called by an async function now performs the coordinated shutdown of
  R-SLIB-PROC-0007: every other task is cancelled and drained, and the process ends with the
  requested status. The same holds for a thread that exits while a task joins it
  (`tests/fixtures/codegen_async_process_exit*.r`).
- The natively implemented fieldless enums of the standard library, such as
  `std.process::termination_kind`, name their variants, are switched over and support
  `core::enum_name` and the other static reflection forms. The runner example now names the
  termination kind through `core::enum_name`.
- On Darwin, a TCP `shutdown` of the read side after the peer's end of stream succeeds: the
  read side is already unable to receive, and `both` publishes the write-side end alone
  (`tests/fixtures/codegen_tcp_shutdown_after_end.r`).
