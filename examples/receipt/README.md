# Reusable receipt templates

Print a customer receipt and an empty-order preview from one captured template. The template
owns a snapshot of the customer's name, so the original owning string can be destroyed before
formatting. Repeated `{1}` slots reuse the same argument and `.2` formats two fractional digits.


The heading uses overloaded builder methods:

```r
std.format::builder builder = std.format::with_capacity(64usize);
builder.append("Receipt for ");
builder.append(customer);
builder.append('\n');
```

The equivalent qualified calls are `std.format::append(&builder, value)`.
Integers keep an explicit radix: `builder.append(quantity, 10u32)`.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_receipt --output-on-failure
build-debug/tests/codegen_example_receipt Ada 3 1.25
```

The sale line is `Ada: 3 x 1.25 = 3.75 (3 units)`. A scratch builder creates the heading, exposes
a borrowed preview, and then reuses its allocation for the body. `finish` transfers the built
string to its new owner. The example displays the unrounded total as well; arithmetic uses f64.
Invalid, negative and overflowing inputs produce a diagnostic.

[main.r](src/main.r) combines named captures, positional arguments, format specifications,
borrowed builder contents, clearing, ownership transfer and checked errors. The numeric
specializations of builder append operations are demonstrated by [Numbers](../numbers/README.md).
