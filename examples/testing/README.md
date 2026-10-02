# Tests of a module with @test

Write the tests of a module in R and run them as a program: `@test` marks a test function
(Core R-FUNC-0025) and `std.test` provides the assertions and the report (Library
R-SLIB-TEST-0001..0003). [version.r](src/version.r) parses, orders and raises versions such as
`1.20.3`; [tests.r](src/tests.r) holds its tests.

```sh
ctest --test-dir build-debug -R 'example_testing' --output-on-failure
build-debug/tests/codegen_example_testing
```

```text
test parses_versions ... ok
test rejects_a_missing_part ... ok
test reports_the_offending_byte ... ok
test orders_versions ... ok
test formats_releases ... ok (2 allocation failures)
test waits_for_a_release ... ok
6 tests: 6 passed, 0 failed
```

`r-front --test` translates the program in test mode: the entry module, here
`example.testing.tests`, declares no `main`, and the compiler adds one that runs the test
functions of the module in declaration order, prints `test NAME ... ` and the outcome of each,
then the summary, and returns 0 when every test passed and 1 otherwise:

```sh
r-front --emit=c17 --test --module-map examples/testing/modules.map \
    --entry example.testing.tests --library-map library/r/library.map > tests.c
```

A test function takes no parameters and returns `void`; it may be asynchronous and may throw
any error it declares. It passes when it returns, and fails with the message of a failed
assertion, the portable name of a standard error or the type of any other error:

```r
@test
void parses_versions() throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    example.testing.version::version value = example.testing.version::parse("1.20.3");
    std.test::equal(value.major, 1u32);
    std.test::equal(value.minor, 20u32);
    std.test::equal(value.patch, 3u32);
}
```

A failed `std.test::equal` reports `FAILED: expected 20, got 21`. The assertions are `check`,
`fail`, `equal` and `not_equal` over values of one type that compare and format,
`equal_text` and `contains`. `@test(expect = E)` passes only when the test throws an error that
a catch of E receives, and `@test(allocations)` runs a passing synchronous test again once for
each allocation it made, with that allocation failing: every such run must end with
`std.alloc::alloc_error`, so the report `ok (2 allocation failures)` means that both failures of
`formats_releases` were handled:

```r
@test(expect = std.convert::parse_error)
void rejects_a_missing_part() throws std.convert::parse_error { ... }

@test(allocations)
void formats_releases() throws std.test::failure, std.alloc::alloc_error { ... }
```

`bump` builds the next release with a struct update (Core R-INIT-0004): the explicit fields
are the raised part and the parts after it, and `...current` supplies the rest:

```r
case part::patch: return version {.patch = current.patch + 1u32, ...current};
```

`std.test::allocation_attempts` and `std.test::fail_allocation_at` are the allocator controls
that the test entry uses for these runs; a test may also call them itself. The standard modules
written in R are tested the same way by the files of `library/r/tests`.
