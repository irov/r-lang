# User-defined generics

Declare type parameters with an `@generic<...>` header:

```r
@generic<T>
struct Vector {
    array<T> values;
};
```

`Vector<T>` owns an `array<T>`. The same `singleton`, `append`, and `count`
definitions work with `i32` and `own i32*`. Type arguments of functions are
inferred from their value arguments. `empty` mentions `T` only in its result, so a call
names it explicitly: `empty::<i32>()`. The same list closes a generic function without
calling it: `auto count_numbers = count::<i32>;` stores a function item for `Vector<i32>`. `move` copies a Copy value and transfers a
Move value. Array cleanup destroys accepted owners automatically; a failed
`push` carries the unaccepted value in `std.array::push_error<T>`.

The vector module is imported from source. Each closed instantiation has its own
concrete representation and functions; no runtime generic metadata is involved.

From the repository root, inspect the generated program with:

```sh
build-debug/r-front --module-map examples/generics/modules.map \
    --entry example.generics.main --emit=c17
ctest --test-dir build-debug -R r_frontend_codegen_generic_example --output-on-failure
```

The runtime test compiles the generated C17 with warnings as errors and runs both
Copy and Move instantiations. `count` borrows the vector, so it remains available
for subsequent operations.
