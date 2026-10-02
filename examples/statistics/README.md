# Measurement reports and lazy iteration

Analyze integer measurements, paginate squared values and build a simple hourly schedule.
The program accepts signed i32 input; sums and squares use i64 to preserve the input range.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_statistics --output-on-failure
build-debug/tests/codegen_example_statistics summary 3 -2 7 0 -9
build-debug/tests/codegen_example_statistics page 1 2 3 -2 7 0 -9
build-debug/tests/codegen_example_statistics sort 3 5 1 3
build-debug/tests/codegen_example_statistics descending 3 5 1 3
build-debug/tests/codegen_example_statistics inspect 9 3 1 2
build-debug/tests/codegen_example_statistics runs 3 5 1 3 2
build-debug/tests/codegen_example_statistics find 3 3 5 1 3 2 3
build-debug/tests/codegen_example_statistics schedule 8 12
```

`summary` reports count, sum, signs, the first negative reading and positions in the original
input. `page OFFSET LIMIT` prints indexed squares for a window and collects rejected negative
readings into a list. `sort`, `descending` and `inspect` take a search value before their readings;
results include extrema, ordering and search positions. A binary search runs only on sorted input.
`schedule` combines two consecutive windows and pairs them with one-based slot numbers.
`runs` prints the maximal non-decreasing runs of the readings, one per row, then their number
and the one-based position of the first longest run. `find WANTED` reports the first position
of a value and the number of its occurrences.

[analysis.r](src/analysis.r) demonstrates lazy sources, adapters and consumers, local callables,
associated iterator items and collection into arrays and lists. [ordering.r](src/ordering.r)
uses borrowed slices for searching and an exclusive slice for sorting. Empty input is supported.
The command tests independently calculate reports, including minimum and maximum i32 values.

The summary helper `count_and_sum` demonstrates two `out` parameters. Both values
are initialized privately and published together on success; the caller uses them
in its summary row.

[segments.r](src/segments.r) keeps the runs in an `array<const i32[]>`. Each element is a view
of the readings, so no reading is copied and the array cannot outlive the readings (Core
R-BORROW-0018). `find` searches both halves of the input concurrently: each half goes to a
scoped task in a `Query` that holds the view together with the value sought and the position
of the half. An ordinary task cannot take a view (Core R-BORROW-0024); a `@scoped async` task
can, because its task group keeps the readings alive until the task ends (Core R-STMT-0017):

```r
struct Query { const i32[] readings; i32 wanted; usize offset; };
@scoped
async Found search(Query query) { ... }
task_scope(2) halves {
    auto front_task = search(front);
    auto back_task = search(back);
    ...
}
```
