module test.codegen.standard_fault;

import std.string;

/* L21.5, Library R-SLIB-ERR-0004: std.error::fault is the root of the standard errors. One throws
   entry and one catch cover them, a rethrow keeps the exact error, an exact standard error
   widens to the root, and from_fault gives the portable error the main boundary would report. */
i32 parse(str text) throws std.error::fault {
    return std.convert::parse_i32(text, 10u32);
}

std.string::string copy(str text) throws std.error::fault {
    return std.string::from_str(text);
}

u32 portable_code(std.error::fault failure) {
    std.error::error portable = std.error::from_fault(failure);
    return portable.code;
}

/* The exact standard error decides after a rethrow in another function. */
i32 triage(std.error::fault failure) {
    try {
        throw move failure;
    } catch (std.convert::parse_error e) {
        return 10;
    } catch (std.alloc::alloc_error e) {
        return 20;
    } catch (std.error::fault other) {
        return 30;
    }
}

i32 main() {
    i32 failures = 0;
    try {
        failures += parse("42") == 42 ? 0 : 1;
        std.string::string text = copy("abc");
        str view = text;
        failures += len(view) == 3usize ? 0 : 2;
        i32 bad = parse("x");
        failures += bad >= 0 ? 4 : 4;
    } catch (std.error::fault e) {
        std.error::error portable = std.error::from_fault(e);
        failures += portable.domain == std.error::domain::conversion ? 0 : 8;
        failures += triage(e) == 10 ? 0 : 16;
    }
    try {
        i32 again = std.convert::parse_i32("", 10u32);
        failures += again >= 0 ? 32 : 32;
    } catch (std.convert::parse_error exact) {
        failures += portable_code(exact) == std.error::from_parse(exact).code ? 0 : 64;
    }
    return failures;
}
