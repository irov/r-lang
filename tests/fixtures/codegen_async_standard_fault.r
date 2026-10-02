module test.codegen.async_standard_fault;

/* L21.5 in asynchronous frames: std.error::fault across awaits, from_fault in a frame and a
   rethrow of the root that keeps the exact standard error (Library R-SLIB-ERR-0004). */
async i32 parse(i32 which) throws std.error::fault {
    str text = "7";
    if (which != 0) { text = "?"; }
    return std.convert::parse_i32(text, 10u32);
}

async i32 relay(i32 which) throws std.error::fault {
    try {
        return await parse(which);
    } catch (std.error::fault failure) {
        std.error::error portable = std.error::from_fault(failure);
        if (portable.domain == std.error::domain::conversion) { throw move failure; }
        return -1;
    }
}

async i32 main() {
    i32 failures = 0;
    try {
        failures += await relay(0) == 7 ? 0 : 1;
        i32 bad = await relay(1);
        failures += bad >= 0 ? 2 : 2;
    } catch (std.convert::parse_error e) {
        failures += 0;
    } catch (std.error::fault e) {
        failures += 4;
    }
    return failures;
}
