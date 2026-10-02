# Checked settings with an error family

Check `KEY VALUE` pairs from the command line and print the resulting settings. The failures of
one argument form a family of errors (Core R-AGG-0011), every standard failure is caught once as
the root of the standard errors, `std.error::fault` (Library R-SLIB-ERR-0004), the settings
declare their default values (Core R-INIT-0004, R-INIT-0005), and the defaults are parsed and
checked while the program is built (Core R-EXPR-0032).

```sh
ctest --test-dir build-debug -R 'example_settings|settings_commands' --output-on-failure
build-debug/tests/codegen_example_settings port 9000 workers 8 mode fast
```

`settings KEY VALUE...` accepts `port` (1 to 65535), `workers` (1 to 64), `mode` (`fast` or
`safe`) and `retries` (a `u8`), and prints `port=P workers=W mode=M retries=R`. A failure of one
argument prints its position, such as `argument 2: port shall be from 1 to 65535`, and exits
with 65; a standard failure prints its portable name and exits with 70.

[rules.r](src/rules.r) declares the family. Each error names its parent after `:`; the fields
of the parent come first, and an initializer names all of them flat:

```r
error setting_error { usize argument; };
error range_error : setting_error { constexpr str key; i64 low; i64 high; };

throw range_error {.argument = argument, .key = key, .low = low, .high = high};
```

`bounded` declares `throws setting_error`, which covers every member. `describe` receives a
`setting_error` value that holds any member, reads the common field `failure.argument`, and then
rethrows it to learn which error it is: `throw move failure` throws the exact error it holds,
and the clause of the nearest ancestor wins whatever the order of the clauses, so the clause for
`setting_error` itself receives only that exact error:

```r
try {
    throw move failure;
} catch (setting_error other) {
    return f"argument {argument}: invalid setting\n";
} catch (range_error e) {
    return f"argument {argument}: {e.key} shall be from {e.low} to {e.high}\n";
}
```

The settings declare what an initialization that omits a field takes: each field names its
initializer, and the mode is an enum whose `@default` variant is its default value. `Settings {}`
is port 8080, one worker, safe mode and no retries:

```r
enum Mode { @default safe, fast };

struct Settings {
    i64 port = 8080;
    i64 workers = 1;
    Mode mode;
    u8 retries = 0;
};
```

The defaults of the program apply settings text to those values and are checked while the
program is built (Core R-EXPR-0032). `parse_defaults` starts from `Settings {}` and throws
members of the same family; the constant initializer runs it during translation, so the program
holds the parsed settings and no parsing happens at run time:

```r
const Settings DEFAULTS = parse_defaults("workers=4 retries=3");
```

A malformed default stops the build with the error it throws. With `port=80800` in the text the
compiler reports

```text
error R-DIAG-CONST-003 [R-EXPR-0032]: translation-time evaluation throws `range_error`
{.argument = 1, .key = "port", .low = 1, .high = 65535} in `decimal` <- `parse_defaults`
```

A retry count is converted by `std.convert::parse_u8` as it is, so a value such as 300 leaves
`read` as a standard error. [main.r](src/main.r) needs one clause for the whole family and one
for every standard failure, and turns the latter into the portable error that the generated
main boundary would report:

```r
} catch (example.settings.rules::setting_error failure) {
    ...
} catch (std.error::fault failure) {
    std.error::error portable = std.error::from_fault(failure);
    constexpr str name = std.error::name(portable);
    ...
}
```
