# Command runner

Run a real command in a selected directory, capture both output streams, or stop a
long-running child and report its terminal status. Arguments go directly to the
executable; the runner does not add a shell.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j 8
ctest --test-dir build-debug --output-on-failure -R '(codegen_example_runner|example_runner_commands)'
build-debug/tests/codegen_example_runner capture / /bin/echo 'hello world'
build-debug/tests/codegen_example_runner capture / /usr/bin/env --clear-env --env MESSAGE 'hello world'
build-debug/tests/codegen_example_runner stop / /bin/sleep 60
```

`run` inherits standard streams. `capture` closes the child's piped stdin and drains
stdout and stderr concurrently, then reports each stream separately. Each stream is
limited to 1 MiB and must contain UTF-8; decoding happens after collection, so a
character may span several reads. This mode is intended for finite commands that do
not need input. `stop` uses null streams, requests termination and waits for the child.

The syntax is `runner MODE WORKING_DIRECTORY EXECUTABLE [ARGUMENT...]`. Before `--`,
`--env NAME VALUE` sets a child environment entry, `--unset NAME` removes one, and
`--clear-env` selects an empty environment. Other operands become command arguments;
`--` makes all remaining operands literal arguments. The parent environment is unchanged.

The final line contains termination kind, native code and success. A nonzero child
exit is a successfully observed outcome: the runner reports it and returns zero.
Runner errors use 64 (usage/capture contract), 65 (path), 69 (process), 71 (allocation),
74 (I/O), or 75 (async start) while capturing child output. Allocation and task-start failures
outside the capture reach the implicit `main` error boundary (112 and 116).

This application demonstrates public process records, exhaustive Move-outcome
switches, returning rejected owners, direct `await`, and named tasks whose work
intentionally overlaps. The end-to-end checks include outputs larger than pipe
capacity, UTF-8 read boundaries, EOF on stdin, environment isolation, a changed
working directory, nonzero exit, missing paths and forceful termination.

Use the [process status probe](../process_status/README.md) as a child to test orderly
nonzero exit and abrupt termination.
