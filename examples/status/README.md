# Device status report with formatted records

Print records as text without writing a line of formatting code per type: `core::Format`
(Core R-TYPE-0046) is the trait of every value that formatted literals can insert. A record
derives it with `@derive(format)` (R-AGG-0012), a type with its own spelling implements it, and
scalars, strings, options, arrays, tuples and network addresses have it already.

```sh
ctest --test-dir build-debug -R 'example_status' --output-on-failure
build-debug/tests/codegen_example_status devices
build-debug/tests/codegen_example_status readings
build-debug/tests/codegen_example_status probe 192.168.1.5 8080
```

`status devices` prints each device of [records.r](src/records.r) through `f"{device}"`:

```text
Device { id: 1, kind: sensor, endpoint: 10.0.0.7:5683, firmware: some(12) }
Device { id: 2, kind: relay, endpoint: 10.0.0.9:502, firmware: none }
Device { id: 3, kind: gateway, endpoint: [fd00::1]:8883, firmware: some(4) }
```

The derived text of a struct is `Name { field: value, ... }`, each field in its own text: the
enum `Kind` writes the name of its variant, the socket address `address:port` (an IPv6 address
in brackets) and the option `some(v)` or `none`:

```r
@derive(format)
enum Kind { sensor, relay, gateway };

@derive(format)
struct Device { u32 id; Kind kind; std.net::socket_address endpoint; o<u32> firmware; };
```

`Celsius` writes its own text. An implementation appends to the builder it receives and may
throw only `std.alloc::alloc_error`:

```r
impl core::Format for Celsius {
    void format(const Celsius* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string text = f"{this->degrees} C";
        std.format::append_str(out, text);
    }
};
```

`status readings` prints labelled columns through one generic function. Its bound admits every
type with a text, and the widths pad on the left to a number of Unicode characters:

```r
@generic<T: core::Format>
std.string::string column(str label, const T* value) throws std.alloc::alloc_error {
    return f"{label:10} {value:24}\n";
}
```

```text
      room      temperature(21.5 C)
    heater switched { relay: 2, on: true }
     attic                  missing
summary: 3, 21.5 C, false
```

A variant with a payload is written `name(payload)`, one with fields `name { field: value }`.
The summary line holds values of three types in one `array<own dyn(core::Format & send)*>`
and appends each with `item->format(&out)`; a number and a Boolean join the interface as
members like `Celsius`.

`status probe ADDRESS PORT` formats a socket address built from its arguments. Invalid input
reports the error and exits with 70; a wrong command line prints the usage and exits with 64.

To compile the program manually (the object goes to standard output; the CTest case
`r_frontend_codegen_example_status` links it with the runtime and the library into
`build/debug/tests/codegen_example_status`):

```sh
build/debug/r-front --module-map examples/status/modules.map \
  --entry example.status.main --library-map library/r/library.map --emit=object > /tmp/status.o
```
