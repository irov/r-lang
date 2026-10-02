module test.codegen.library_time_interval;

import std.time;

// R-SLIB-TIME-0009 (M21): tick n completes no earlier than the start plus n periods; a call
// before the next tick waits for it, a call after it completes the latest due tick at once and
// skips the ticks between; ticks keep their grid while other tasks keep the executor busy; the
// period is validated and a cancelled tick leaves the interval unchanged.

protected i64 nanoseconds_since(std.time::instant start) throws std.time::time_error {
    std.time::instant now = std.time::monotonic_now();
    std.time::duration elapsed = std.time::instant_duration(now, start);
    return std.time::duration_seconds(elapsed) * 1000000000i64 +
           std.time::duration_nanoseconds(elapsed) as i64;
}

protected std.time::duration milliseconds(i64 count) throws std.time::duration_error {
    return std.time::duration_from_parts(count / 1000i64, ((count % 1000i64) * 1000000i64) as u32);
}

protected i32 rejects(std.time::duration period) {
    try {
        std.time::interval::every(period) as void;
        return 1;
    } catch (std.time::time_error failure) {
        if (failure.code != std.time::error_code::invalid_value) { return 2; }
        return 0;
    }
}

/* Periods: positive and below 2^63 nanoseconds. */
protected i32 periods() throws std.time::duration_error, std.time::time_error {
    if (rejects(std.time::duration_from_seconds(0i64)) != 0) { return 1; }
    if (rejects(std.time::duration_from_seconds(-1i64)) != 0) { return 2; }
    if (rejects(std.time::duration_from_parts(9223372036i64, 854775808u32)) != 0) { return 3; }
    std.time::interval widest =
        std.time::interval::every(std.time::duration_from_parts(9223372036i64, 854775807u32));
    widest as void;
    std.time::interval narrowest = std.time::interval::every(std.time::duration_from_parts(0i64, 1u32));
    narrowest as void;
    return 0;
}

/* A grid of 200 ms: two ticks on time, a delay of three and a half periods, one late tick at
   once, then the next tick on the grid again. */
protected async i32 timeline()
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    i64 period = 200000000i64;
    std.time::instant start = std.time::monotonic_now();
    std.time::interval ticker = std.time::interval::every(milliseconds(200i64));
    task_scope(1) scope {
        u64 first = await ticker.tick();
        i64 first_at = nanoseconds_since(start);
        i32 status = 0;
        if (first != 1u64) { status = 11; }
        if (first_at < period) { status = 12; }
        if (status != 0) { return status; }
        u64 second = await ticker.tick();
        i64 second_at = nanoseconds_since(start);
        if (second != 2u64) { status = 13; }
        if (second_at < 2i64 * period) { status = 14; }
        if (status != 0) { return status; }
        await std.time::sleep_for(milliseconds(700i64));
        i64 before = nanoseconds_since(start);
        u64 late = await ticker.tick();
        i64 after = nanoseconds_since(start);
        /* At least 1100 ms have passed: ticks 3 and 4 were due, tick 5 is due at 1000 ms. */
        if (late < 5u64) { status = 15; }
        if ((late as i64) * period > after) { status = 16; }
        /* At once: a waiting tick would take until the next grid point, at least 100 ms away. */
        if (after - before > period / 2i64) { status = 17; }
        if (status != 0) { return status; }
        u64 next = await ticker.tick();
        i64 next_at = nanoseconds_since(start);
        if (next <= late) { status = 18; }
        if ((next as i64) * period > next_at) { status = 19; }
        return status;
    }
}

/* Work that yields to the executor between rounds. */
protected async u64 churn(u32 rounds) throws std.time::time_error, std.async::start_error {
    u64 total = 0u64;
    for (u32 round = 0u32; round < rounds; round += 1u32) {
        for (u64 index = 0u64; index < 20000u64; index += 1u64) { total += index ^ (total >> 3u64); }
        await std.time::sleep_for(std.time::duration_from_seconds(0i64));
    }
    return total;
}

/* Ticks of 10 ms under load: numbers grow and no tick completes before its time. */
protected async i32 steady()
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    i64 period = 10000000i64;
    std.time::instant start = std.time::monotonic_now();
    std.time::interval ticker = std.time::interval::every(milliseconds(10i64));
    task_scope(1) scope {
        u64 last = await ticker.tick();
        if ((last as i64) * period > nanoseconds_since(start)) { return 21; }
        for (u32 count = 1u32; count < 30u32; count += 1u32) {
            u64 number = await ticker.tick();
            i64 now = nanoseconds_since(start);
            bool early = (number as i64) * period > now;
            if (number <= last) { return 22; }
            if (early == true) { return 23; }
            last = number;
        }
        if (last < 30u64) { return 24; }
        return 0;
    }
}

/* A tick that loses a race is cancelled and leaves the interval as it was: the next tick is
   still tick 1. */
protected async i32 cancelled()
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    std.time::interval slow = std.time::interval::every(milliseconds(300i64));
    std.time::instant soon = std.time::monotonic_now().add(milliseconds(20i64));
    i32 status = 0;
    task_scope(1) group {
        auto pending = slow.tick();
        select (group) {
        case u64 number = await move pending: number as void; status = 31; break;
        case until (soon): break;
        }
        group.cancel_all();
        await group.all();
    }
    if (status != 0) { return status; }
    task_scope(1) scope {
        u64 first = await slow.tick();
        if (first != 1u64) { return 32; }
    }
    return 0;
}

async i32 main() {
    try {
        i32 checked = periods();
        if (checked != 0) { return checked; }
        i32 simple = await timeline();
        if (simple != 0) { return simple; }
        task_scope(3) load {
            auto measured = steady();
            auto load_one = churn(300u32);
            auto load_two = churn(300u32);
            i32 ticked = await move measured;
            u64 one = await move load_one;
            u64 two = await move load_two;
            i32 status = ticked;
            if (one == 0u64) { status = 25; }
            if (two == 0u64) { status = 26; }
            if (status != 0) { return status; }
        }
        return await cancelled();
    } catch (std.time::time_error failure) {
        failure as void;
        return 90;
    } catch (std.time::duration_error failure) {
        failure as void;
        return 91;
    } catch (std.async::start_error failure) {
        failure as void;
        return 92;
    }
}
