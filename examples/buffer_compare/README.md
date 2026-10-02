# Compare buffers with explicit ownership

A buffer ownership demonstration that compares two command-line strings and reports only their
byte lengths and equality. One buffer is created with its final length; the other adopts an
existing byte allocation. Both use the secret-buffer destructor and are explicitly erased before
normal return.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_buffer_compare --output-on-failure
build-debug/tests/codegen_example_buffer_compare sample sample
build-debug/tests/codegen_example_buffer_compare sample different
```

[compare.r](src/compare.r) groups the buffers in one owning request with `std.alloc::try_new`.
It then transfers ownership through an opaque raw token, recovers it with `core::adopt`, and
extracts the completed request with `std.alloc::into_value`. No raw pointer is dereferenced.
The token is consumed once and retains its allocation type and destruction contract.

The comparison borrows both buffers. Mutable views used for erasure end before destruction.
If request allocation fails, `std.alloc::new_error<Pair>` retains the rejected buffers and
cleans them up when caught. Input comes from command-line arguments; erasure here applies to
the separately owned buffers, not the original argument storage.
