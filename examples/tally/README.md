# Labelled report columns with tuples and type packs

Summarize the numbers given on the command line and print them as labelled columns. A summary
is one tuple (Core R-TYPE-0052), and the columns are a tuple spread into a function over a type
pack, which renders any number of columns of different types (Core R-TYPE-0053).

```sh
ctest --test-dir build-debug -R 'example_tally|tally_commands' --output-on-failure
build-debug/tests/codegen_example_tally 1 2 3
```

`tally VALUE...` prints `count=N  sum=S  min=L  max=H  even=yes|no`: the number of values,
their sum, the least and the greatest value and whether the sum is even. A value that is not
an integer exits with 65.

[cells.r](src/cells.r) returns the summary as a tuple, and [main.r](src/main.r) destructures it
into one local per element (Core R-STMT-0022):

```r
(usize, i64, i64, i64) summarize(const i64[] values) { ... return (len(values), total, low, high); }

auto (entries, sum, low, high) = example.tally.cells::summarize(values.as_slice());
```

A column is a `Named<T>` value whose type implements the trait `Cell`. `line` takes a first
column and a pack of further columns; each element of the pack proves `Cell`. The recursion
spreads the pack into the next call, which takes its first element as its own first column,
and the static branch `@if (len(Tail...) != 0usize)` ends it, so each number of columns gets
one function:

```r
@generic<Head: Cell, Tail...: Cell>
std.string::string line(Head head, Tail... tail) throws std.alloc::alloc_error {
    std.string::string text = head.render();
    @if (len(Tail...) != 0usize) {
        std.string::string rest = line(...move tail);
        return f"{text}  {rest}";
    }
    ...
}
```

[main.r](src/main.r) builds the five columns as one tuple of different `Named<T>` types and
spreads it into `line(...columns)`; the spread stands for the elements of the tuple as the
arguments of the call.
