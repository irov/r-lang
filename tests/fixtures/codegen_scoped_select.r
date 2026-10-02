module test.codegen.scoped_select;

/* R-STMT-0018: bounded group waits, select, and explicit fate of the unselected members. */
struct Counter { atomic u32 drops; };
struct Tracked { arc Counter counter; };
drop(Tracked* self) {
    const Counter* counter = &*self->counter;
    core::atomic_fetch_add(&counter->drops, 1u32, core::memory_order::relaxed) as void;
}
error Failed { Tracked payload; };

async void pause() {}

@scoped
async u32 compute(const u32* value) throws std.async::start_error {
    await pause();
    return *value;
}

@scoped
async u32 sleeper(i32* cleanups, std.time::duration delay)
    throws std.async::start_error, std.time::time_error {
    try { await std.time::sleep_for(delay); }
    finally { *cleanups += 1; }
    return 7u32;
}

async u32 failing(arc Counter counter) throws Failed, std.async::start_error {
    await pause();
    throw Failed {.payload = Tracked {.counter = move counter}};
}

// A timer against a computation: readiness waits return early and consume nothing.
async i32 bounded(std.time::duration long_time)
    throws std.async::start_error, std.time::time_error, std.time::duration_error {
    u32 input = 5u32;
    i32 cleanups = 0;
    i32 status = 0;
    std.time::instant start = std.time::monotonic_now();
    std.time::instant soon =
        std.time::instant_add(start, std.time::duration_from_parts(0i64, 20000000u32));
    std.time::instant late = std.time::instant_add(start, std.time::duration_from_seconds(10i64));
    task_scope(2) group {
        auto slow = sleeper(&cleanups, long_time);
        auto fast = compute(&input);
        o<usize> early = await group.first_until(soon, &slow);
        switch (early) {
        case variant o::some(index): status = 1; break;
        case variant o::none: break;
        }
        o<usize> ready = await group.first_until(late, &fast, &slow);
        switch (ready) {
        case variant o::some(index): if (*index != 0usize) { status = 2; } break;
        case variant o::none: status = 3; break;
        }
        bool finished = await group.all_until(soon);
        if (finished == true) { status = 4; }
        u32 value = await move fast;
        if (value != 5u32) { status = 5; }
        // Delayed acknowledgement: the request returns at once, the barrier awaits the cleanup.
        group.cancel_all();
        group.cancel_all();
        await group.all();
        if (cleanups != 1) { status = 6; }
    }
    if (cleanups != 1) { status = 7; }
    return status;
}

// Members that are all terminal: the first argument and the first branch win.
async i32 simultaneous() throws std.async::start_error {
    u32 left = 1u32;
    u32 right = 2u32;
    i32 status = 0;
    task_scope(2) group {
        auto a = compute(&left);
        auto b = compute(&right);
        await group.all();
        usize both = await group.first(&b, &a);
        if (both != 0usize) { status = 1; }
        select (group) {
        case u32 value = await move b:
            if (value != 2u32) { status = 2; }
            break;
        case u32 value = await move a:
            if (value != 0u32) { status = 3; }
            break;
        }
        group.cancel_all();
    }
    return status;
}

// Errors of several children: the selected error propagates, the other payload is destroyed once.
async i32 several_errors(arc Counter counter) throws std.async::start_error {
    i32 status = 0;
    try {
        task_scope(2) group {
            auto first = failing(std.arc::clone(&counter));
            auto second = failing(std.arc::clone(&counter));
            await group.all();
            select (group) {
            case u32 value = await move first:
                if (value != 0u32) { status = 1; }
                break;
            case u32 value = await move second:
                if (value != 0u32) { status = 2; }
                break;
            }
            status += 3;
        }
    } catch (Failed failure) {
        if (core::atomic_load(&counter->drops, core::memory_order::relaxed) != 1u32) {
            status = 4;
        }
    }
    if (core::atomic_load(&counter->drops, core::memory_order::relaxed) != 2u32) { status = 5; }
    return status;
}

// A deadline branch: the loser keeps running until the group exit cancels it exactly once.
async i32 deadline_select(std.time::duration long_time)
    throws std.async::start_error, std.time::time_error, std.time::duration_error {
    i32 cleanups = 0;
    i32 status = 0;
    std.time::instant soon = std.time::instant_add(std.time::monotonic_now(),
                                                   std.time::duration_from_parts(0i64, 20000000u32));
    task_scope(1) group {
        auto slow = sleeper(&cleanups, long_time);
        select (group) {
        case u32 value = await move slow:
            if (value != 0u32) { status = 1; }
            break;
        case until (soon):
            break;
        }
    }
    if (cleanups != 1) { status = 2; }
    return status;
}

async i32 main() {
    try {
        try {
            std.time::instant started = std.time::monotonic_now();
            std.time::duration long_time = std.time::duration_from_seconds(30i64);
            arc Counter counter = new arc Counter {.drops = 0u32};
            i32 timed = await bounded(long_time);
            if (timed != 0) { throw TestAssertionFailed {.code = 10 + timed}; }
            i32 tie = await simultaneous();
            if (tie != 0) { throw TestAssertionFailed {.code = 20 + tie}; }
            i32 errors = await several_errors(std.arc::clone(&counter));
            if (errors != 0) { throw TestAssertionFailed {.code = 30 + errors}; }
            i32 deadline = await deadline_select(long_time);
            if (deadline != 0) { throw TestAssertionFailed {.code = 40 + deadline}; }
            std.time::instant finished = std.time::monotonic_now();
            std.time::duration elapsed = finished.duration(started);
            if (elapsed.seconds() >= 10i64) { throw TestAssertionFailed {.code = 50}; }
            return 0;
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 60}; }
        catch (std.time::time_error failure) { throw TestAssertionFailed {.code = 61}; }
        catch (std.time::duration_error failure) { throw TestAssertionFailed {.code = 62}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
