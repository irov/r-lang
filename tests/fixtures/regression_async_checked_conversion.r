module test.regression.async_checked_conversion;

// M23-1: std.convert::checked_D and std.c::checked_D in an asynchronous function were rejected
// by the C17 lowering; each value keeps its exact meaning and a failure reaches the catch with
// its range_error, also across an await.

struct Count { u64 value; };

async u64 pause(u64 value) {
    return value;
}

async i32 checks() throws std.async::start_error {
    i32 status = 0;
    u64 wide = await pause(4000000000u64);
    try {
        u32 narrow = std.convert::checked_u32(wide);
        if (narrow != 4000000000u32) { status += 1; }
        i64 signed_value = std.convert::checked_i64(wide);
        if (signed_value != 4000000000i64) { status += 2; }
        f64 floating = std.convert::checked_f64(wide);
        u64 back = await pause(std.convert::checked_u64(floating));
        if (back != wide) { status += 4; }
        c_int c_value = std.c::checked_c_int(-7i32);
        i32 restored = std.convert::checked_i32(c_value);
        if (restored != -7) { status += 8; }
    } catch (std.convert::range_error failure) {
        failure as void;
        status += 16;
    }
    try {
        i32 too_large = std.convert::checked_i32(wide);
        too_large as void;
        status += 32;
    } catch (std.convert::range_error failure) {
        if (failure != std.convert::range_error::above_maximum) { status += 64; }
    }
    Count seen = {.value = 0u64};
    try {
        seen.value = await pause(1u64);
        i8 negative = std.convert::checked_i8(-200i32);
        negative as void;
        status += 128;
    } catch (std.convert::range_error failure) {
        if (failure != std.convert::range_error::below_minimum || seen.value != 1u64) {
            status += 256;
        }
    }
    try {
        u8 fraction = std.convert::checked_u8(2.5);
        fraction as void;
        status += 512;
    } catch (std.convert::range_error failure) {
        if (failure != std.convert::range_error::fractional) { status += 1024; }
    }
    return status;
}

async i32 main() {
    return await checks();
}
