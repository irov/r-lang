# Calendar conversion, date texts, durations and intervals

Convert Unix timestamps to UTC, check calendar dates, write and read RFC 3339 and HTTP dates,
calculate exact nanosecond durations, measure an async deadline, tick a periodic interval, and
show local time in time zones.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_clock --output-on-failure
build-debug/tests/codegen_example_clock now
build-debug/tests/codegen_example_clock utc 2000 2 29 12 34 56 0
build-debug/tests/codegen_example_clock shift 0 0 -86400
build-debug/tests/codegen_example_clock add 1 250000000 2 750000000
build-debug/tests/codegen_example_clock mul -1 250000000 3
build-debug/tests/codegen_example_clock sleep 15
build-debug/tests/codegen_example_clock rfc3339 784111777 123456789 3
build-debug/tests/codegen_example_clock parse 'Sunday, 06-Nov-94 08:49:37 GMT'
build-debug/tests/codegen_example_clock ticks 4 50 125
build-debug/tests/codegen_example_clock zone Asia/Shanghai 1700000000
build-debug/tests/codegen_example_clock zones 1700000000
build-debug/tests/codegen_example_clock wall America/New_York 2024 11 3 1 30 0
build-debug/tests/codegen_example_clock tzif /usr/share/zoneinfo/Europe/Berlin 1700000000
TZ=Europe/Kyiv build-debug/tests/codegen_example_clock local 1700000000
```

Durations are normalized as signed seconds plus unsigned nanoseconds below one billion.
For example, `-1 250000000` means -0.75 seconds. `add`, `sub`, `compare` and `mul` operate on
these components without floating-point conversion. Calendar output includes the UTC components
and the timestamp recovered from them.

`sleep MILLISECONDS` waits using the monotonic clock and prints measured elapsed time. Its demo
limit is 5000 milliseconds. The program uses both direct `await` forms: a standalone wait and
a complete local initializer for an async result. Measurements may exceed the requested time.

`rfc3339 UNIX_SECONDS NANOSECONDS DIGITS` writes the time as RFC 3339 in UTC with up to nine
digits of the fraction, and `http UNIX_SECONDS` as an HTTP date (Library R-SLIB-TIME-0006 and
R-SLIB-TIME-0008). `parse TEXT` reads either form: a text that begins with a digit is RFC 3339,
with `T`, `t` or a space and an optional offset; any other is one of the three HTTP forms, as a
server must accept them in `If-Modified-Since`. An invalid text reports the code and byte index
of its first error:

```text
$ clock parse 'Sunday, 06-Nov-94 08:49:37 GMT'
unix=784111777 0
rfc3339=1994-11-06T08:49:37Z
$ clock parse 1994-13-06T08:49:37Z
domain=conversion family=primary name=above_maximum code=5 native_code=0 index=5
```

`ticks COUNT MILLISECONDS [PAUSE]` runs a `std.time::interval` (R-SLIB-TIME-0009): tick n ends
no earlier than n periods after the start, so the ticks keep to their grid however long the
program works between them. A pause after the first tick makes the next one late; it then
completes at once as the latest due tick and the ticks in between are skipped, instead of
arriving as a burst:

```text
$ clock ticks 4 50 125
ticks: 1 3 4 5
elapsed=250 ms
```

[operations.r](src/operations.r) implements conversions, date texts, intervals and waiting; [main.r](src/main.r)
handles all commands and checked errors. Tests cover leap years, invalid dates, negative Unix
timestamps, normalization, overflow, real suspension and the current system clock.

## Time zones

[zones.r](src/zones.r) works with the time zones of `std.time` (Library R-SLIB-TIME-0010..0013).
`zone NAME UNIX_SECONDS` loads a zone of the IANA database from the zone files of the system and
prints the local time of the instant, the start of its local day and the instant one month
later:

```text
Asia/Shanghai: 2023-11-15 6:13:20 CST offset=28800 dst=false weekday=3 day=319
day starts 1699977600 and lasts 24 hours
next month 1702592000
```

A day on which daylight saving time starts or ends lasts 23 or 25 hours. `zones UNIX_SECONDS`
shows one instant in UTC, in a zone of a fixed offset and in the zone of a POSIX TZ string:

```r
std.time::zone utc = std.time::utc_zone();
std.time::zone india = std.time::fixed_zone(19800i32, "IST");
std.time::zone sydney = std.time::posix_zone("AEST-10AEDT,M10.1.0,M4.1.0/3");
```

`wall NAME Y M D h m s` resolves a local time with each `std.time::ambiguity`. In New York
01:30 on 2024-11-03 happens twice and 02:30 on 2024-03-10 never; `earlier` and `later` read a
skipped time with the offset before or after the change, as fold 0 and fold 1 do in Python:

```text
earlier=1730611800 later=1730615400 reject=ambiguous_time
earlier=1710055800 later=1710052200 reject=nonexistent_time
```

`tzif PATH UNIX_SECONDS` reads a TZif file from anywhere with `std.time::parse_tzif`, and
`local UNIX_SECONDS` uses the zone of the process: the zone or POSIX TZ string of `TZ`, or
`/etc/localtime`. An unknown zone name ends with `zone: unknown_zone`, a name that is not a
relative path of zone names with `zone: invalid_name`, both with status 65. The behaviour test
compares every line with the Python zoneinfo module.

