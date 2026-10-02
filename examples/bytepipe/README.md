# Bounded byte pipe

Copy at most 1 MiB from stdin to stdout, optionally validating UTF-8 without
copying its owner into a second string allocation. Diagnostics go to stderr.
Input is collected in bounded chunks; the complete bounded document is retained
before output begins. This is not an unbounded streaming copier.

```sh
ctest --test-dir build-debug --output-on-failure -R '(codegen_example_bytepipe|example_bytepipe_commands)'
printf 'hello world\n' | build-debug/tests/codegen_example_bytepipe plain
printf 'valid UTF-8\n' | build-debug/tests/codegen_example_bytepipe text
printf 'shared payload\n' | build-debug/tests/codegen_example_bytepipe shared
```

- `plain` preserves arbitrary bytes and handles partial output progress by retrying
  only the unwritten suffix.
- `shared` transfers an `arc(bytes)` owner to `write_shared` and observes the
  returned owner before finishing the output stream.
- `text` uses a synchronous validator and consumes `from_bytes_result`. A valid
  string is converted back into bytes through the same allocation.
- `text_async` makes the same outcome switch inside the async function, illustrating
  identical ownership behavior across the two backends.

UTF-8 validation happens after reading, so code points may span chunk boundaries.
Invalid input reports its first invalid byte, returns 65 and produces no stdout.
Input-size errors also return 65; command and I/O failures return 64 and 74. Allocation
and task-start failures reach the implicit `main` error boundary (112 and 116). Shared-owner construction uses ordinary `new arc`
allocation semantics.

This example covers standard input/output ownership, read and write outcomes,
partial writes, shared writes, explicit flush/close, UTF-8 validation and exact
transfer or destruction of returned payload owners.
