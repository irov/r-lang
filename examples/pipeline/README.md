# Generic measurement pipeline with checked errors

Bound every reading, scale it and summarize the results. Each stage is an ordinary callable
that throws only its own checked error; the generic pipeline that runs them catches nothing,
so its error set is exactly the union of the stages' sets (Core R-TYPE-0049).

```sh
ctest --test-dir build-debug -R 'example_pipeline|pipeline_commands' --output-on-failure
build-debug/tests/codegen_example_pipeline 10 2 3 -4 5
build-debug/tests/codegen_example_pipeline 10 0 3 -4 5
```

`pipeline LIMIT FACTOR VALUE...` admits values in `[-LIMIT, LIMIT]`, multiplies them by
`FACTOR` (or takes their magnitude when `FACTOR` is 0) and prints `count=N sum=S max=M`. A
value outside the limit exits with 66, an overflowing product or sum with 67 and an invalid
number with 65.

[stages.r](src/stages.r) builds the stages. `bounded` and `scaled` return opaque callables
that keep their parameter by value; `magnitude` is a plain function passed by name:

```r
opaque(fn(i64) -> i64 throws(OutOfRange) & copy) bounded(i64 limit) { ... }
opaque(fn(i64) -> i64 throws(Overflow) & copy) scaled(i64 factor) { ... }
i64 magnitude(i64 value) { ... }

@generic<E: errors & unborrowed, G: errors & unborrowed,
         F: fn(i64) -> i64 throws(E), H: fn(i64) -> i64 throws(G)>
void run(i64[] values, F first, H second) throws E, G { ... }
```

`run(values, admit, scaled(factor))` throws `OutOfRange` and `Overflow`;
`run(values, admit, magnitude)` throws only `OutOfRange`, because `magnitude` has no checked
errors. `fold` combines the results with `add`, which reports an overflowing total. Each
instance is closed with its concrete stage types, so the calls are direct and the error sets
are exact at every call site. [main.r](src/main.r) catches each error by name.
