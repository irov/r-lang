# C callbacks and owned handles

This executable example uses exact C function pointers, nullable callbacks, and
`std.c::handle` to manage an external resource obligation.

```sh
r-front --emit=c17 examples/c_callbacks/main.r
ctest --test-dir build-debug --output-on-failure -R r_frontend_codegen_c_callbacks_example
```

`raw fn(c_int) -> c_int` stores a C function pointer. A matching exported
`extern "C"` function marked `@callback` can initialize it. `@safety` documents
the caller's runtime, lifetime, and representation obligations. Converting the
function name is safe; calling through the pointer requires `unsafe`. Nullable
callbacks use `raw fn?(...) -> ...` and can store `null`.

`std.c::adopt_handle` takes a non-null data pointer and an exact
`raw fn(raw void*) -> void` destructor. It assumes the obligation to invoke that
destructor once. Moving the handle transfers the obligation, and scope exit
executes it. `std.c::handle_pointer` borrows the address without transferring
ownership. `std.c::release_handle(move handle)` returns the address without
calling the destructor, so the caller must complete the release protocol.

To keep the example self-contained, the resource starts as a real R allocation.
`core::release` transfers it to the handle, and the exported destructor uses
`core::adopt` to reclaim and free that same allocation. A foreign library would
instead supply its own matching creation and destruction operations. Stack
addresses must not be adopted as R allocations.

Raw function pointers and handles are neither Send nor Sync. Keep them within
one thread and end their lifetimes before an async suspension. Raw C callbacks
carry no captured environment or checked-error set.
