# Process status probe

A child program for testing a command runner or supervisor. Pass a signed integer
for orderly process exit with that status, or `abort` for abnormal termination
without R cleanup. No operand returns zero; invalid input returns 64.

```sh
ctest --test-dir build-debug --output-on-failure -R '(codegen_example_process_status|example_process_status_commands)'
build-debug/tests/codegen_example_process_status 23
build-debug/tests/codegen_example_runner capture / "$PWD/build-debug/tests/codegen_example_process_status" 23
```

This example deliberately uses a synchronous entry. Async programs normally finish
by returning from `main`; the outstanding worker-thread `std.process::exit` problem
is recorded in [known limitations](../known-limitations.md).

`abort` is explicit fault injection. The host may produce a crash report; the command
is useful for checking how supervisors distinguish signals from ordinary exit codes.
