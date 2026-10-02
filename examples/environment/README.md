# Environment inspector

Inspect a named variable, alter a child-local value, count environment entries, or
show an owned argument snapshot. Changes affect this program's process only; they
do not update the invoking shell. `summary` prints a count without dumping values.

```sh
ctest --test-dir build-debug --output-on-failure -R '(codegen_example_environment|example_environment_commands)'
build-debug/tests/codegen_example_environment set R_EXAMPLE_MODE development
build-debug/tests/codegen_example_environment get R_EXAMPLE_MODE
build-debug/tests/codegen_example_environment arguments one 'two words'
build-debug/tests/codegen_example_environment summary
```

The `set` and `unset` commands read the resulting value in the same invocation.
Missing variables are printed as `<absent>`; an empty value remains an empty value.
`get` tests the optional value in its condition (Core R-STMT-0002):
`if (std.env::get(name) is variant o::some(move value)) { ... }`.
Arguments and environment snapshots own their strings independently of runtime
storage. Invalid variable names produce a checked environment error and exit 65.
Other invalid command syntax exits 64; allocation and start errors reach the implicit `main`
error boundary (112 and 116).

The example covers every public `std.env` operation, optional Move values, array
iteration, dictionary counts and conversion to general diagnostic errors.
