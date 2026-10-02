# Formatted strings

`std.format` is the module; `std.format::format` is the type of a stored template.
`std.string::string` is the owning string returned by formatting.

```r
std.string::string name = std.string::from_str("Ada");
std.string::string greeting = f"hello {name}";
std.format::format f0 = f"{name} {1} {2} {1}";
drop name;
std.string::string first = f0.format(1, 2);       // Ada 1 2 1
std.string::string second = f0.format("A", "B"); // Ada A B A
```

Named values are captured when the template is created. Runtime text is copied,
so the template is independent of later changes and the source's lifetime.
Positional arguments are numbered from 1; a repeated index reuses the value already
evaluated. Every supplied argument must be used. Owners are borrowed;
`move` is forbidden in `.format` arguments. A template can be moved to another local
variable; it cannot be passed as a parameter, returned, or stored in a field or container.

Strings, `char`, Boolean, and numeric R/C types are supported, and so is every value whose
type satisfies `core::Format`: records that derive or implement it, options, arrays, slices,
tuples and network addresses (see [status](../status/README.md)). Slots accept local names,
`this` and fields: `{error.message}`, `{record->offset}`. Expressions are passed
positionally: `f"count={1}".format(count + 1)`. Nested formatting is also supported.
Creating a template and producing a string require handling `std.alloc::alloc_error`.

Numbers support `d`, `x`, `X`, `b`, `o`, minimum width (`8x`), zero padding
(`08X`), and fixed floating-point precision (`.2`, `010.2`). A width alone (`{name:12}`)
pads any value on the left to that many Unicode characters. Rounding is to the nearest
value, with ties to an even last digit. `{{` and `}}` produce braces;
ordinary literals still output `{name}` literally.

[main.r](main.r) is an executable example of template reuse, a string snapshot,
and a dynamic `error` message. ZIP/unzip diagnostics also use an f-literal;
the `ZipError.message` field retains its `constexpr str` type.

```sh
build-debug/r-front --emit=c17 examples/format/main.r > /tmp/r-format.c
ctest --test-dir build-debug -R 'r_frontend_codegen_((async_)?format_literals|format_example)' --output-on-failure
```

Runtime tests cover both execution models and inject failure at each allocation
in turn. No snapshot or result owner may remain after a run. Tests also cover
retrying after an allocation failure and cancelling an async task with `finally`
execution. The template in the async test survives across `await`.
