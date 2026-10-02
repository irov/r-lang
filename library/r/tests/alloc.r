module tests.std.alloc;
import std.test;
import std.alloc;

// The tests of the R part of std.alloc (Library R-SLIB-ALLOC-0003..0004) and of budget blocks
// (Core R-STMT-0020), run in test mode (Core R-FUNC-0025).

/* Whether the limits set a bound of that kind. */
protected bool present(o<usize> bound) {
    switch (bound) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

@test
void limits_default_to_none() throws std.test::failure, std.alloc::alloc_error {
    std.alloc::limits open = {};
    std.test::check(present(open.bytes) == false, "bytes without a limit");
    std.test::check(present(open.tasks) == false, "tasks without a limit");
    std.alloc::limits bounded = {.bytes = o::some(64usize)};
    std.test::check(present(bounded.bytes), "a byte limit");
    std.test::check(present(bounded.tasks) == false, "tasks still without a limit");
}

/* 0 when the bytes were allocated, 1 when the budget refused them, 2 for another failure. */
protected i32 try_bytes(usize count) {
    try {
        array<u8> data = std.alloc::bytes(count, 0u8);
        drop data;
        return 0;
    } catch (std.alloc::alloc_error failure) {
        if (failure == std.alloc::alloc_error::budget_exhausted) { return 1; }
    }
    return 2;
}

@test
async void budgets_refuse_and_return_bytes() throws std.error::fault, std.test::failure {
    budget (std.alloc::limits {.bytes = o::some(512usize)}) {
        i32 alone = try_bytes(400usize);
        std.test::equal(alone, 0);
        i32 beyond = try_bytes(600usize);
        std.test::equal(beyond, 1);
        array<u8> held = std.alloc::bytes(300usize, 1u8);
        i32 beside = try_bytes(300usize);
        std.test::equal(beside, 1);
        drop held;
        i32 after = try_bytes(300usize);
        std.test::equal(after, 0);
    }
    i32 outside = try_bytes(100000usize);
    std.test::equal(outside, 0);
}

protected async u32 next(u32 value) { return value + 1u32; }

@test
async void budgets_limit_tasks() throws std.error::fault, std.test::failure {
    budget (std.alloc::limits {.tasks = o::some(1usize)}) {
        u32 first = await next(1u32);
        std.test::equal(first, 2u32);
        u32 second = await next(2u32);
        std.test::equal(second, 3u32);
        task_scope(2) group {
            auto running = next(3u32);
            bool refused = false;
            try {
                auto beside = next(4u32);
                u32 other = await move beside;
                other as void;
            } catch (std.async::start_error failure) {
                refused = failure == std.async::start_error::budget_exhausted;
            }
            std.test::check(refused, "a second task beside the first");
            u32 value = await move running;
            std.test::equal(value, 4u32);
        }
    }
}

/* The value of a bound, or the largest usize for none. */
protected usize bound_of(o<usize> bound) {
    switch (bound) {
    case variant o::some(value): return *value;
    case variant o::none: break;
    }
    return 18446744073709551615usize;
}

/* The tasks counted under the budget of the calling task. */
protected async usize tasks_now() {
    o<std.alloc::usage> now = std.alloc::budget_usage();
    switch (now) {
    case variant o::some(seen): return seen->tasks;
    case variant o::none: break;
    }
    return 0usize;
}

@test
async void reports_the_budget_usage() throws std.error::fault, std.test::failure {
    o<std.alloc::usage> outside = std.alloc::budget_usage();
    switch (outside) {
    case variant o::some(seen): std.test::check(false, "no usage outside every budget");
    case variant o::none: break;
    }
    budget (std.alloc::limits {.bytes = o::some(4096usize), .tasks = o::some(3usize)}) {
        array<u8> held = std.alloc::bytes(1000usize, 1u8);
        o<std.alloc::usage> now = std.alloc::budget_usage();
        switch (now) {
        case variant o::some(seen):
            std.test::equal(seen->bytes, 1000usize);
            std.test::equal(seen->tasks, 0usize);
            std.test::equal(bound_of(seen->byte_limit), 4096usize);
            std.test::equal(bound_of(seen->task_limit), 3usize);
            std.test::equal(bound_of(seen->bytes_available), 3096usize);
        case variant o::none: std.test::check(false, "usage inside a budget");
        }
        /* The innermost budget is reported; its room is the least room of the budgets it is
           nested in. */
        budget (std.alloc::limits {.bytes = o::some(10000usize)}) {
            o<std.alloc::usage> nested = std.alloc::budget_usage();
            switch (nested) {
            case variant o::some(seen):
                std.test::equal(seen->bytes, 0usize);
                std.test::equal(bound_of(seen->byte_limit), 10000usize);
                std.test::equal(bound_of(seen->task_limit), 18446744073709551615usize);
                std.test::equal(bound_of(seen->bytes_available), 3096usize);
            case variant o::none: std.test::check(false, "usage inside a nested budget");
            }
        }
        usize counted = await tasks_now();
        std.test::equal(counted, 1usize);
        drop held;
        o<std.alloc::usage> released = std.alloc::budget_usage();
        switch (released) {
        case variant o::some(seen): std.test::equal(seen->bytes, 0usize);
        case variant o::none: std.test::check(false, "usage after a release");
        }
    }
}
