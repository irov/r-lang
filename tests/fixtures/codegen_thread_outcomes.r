module test.thread_outcomes;
error Failure { own i32* value; };
i32 copy_worker() { return 41; }
own i32* owner_worker() { return new i32(42); }
void void_worker() { return; }
own i32* checked_worker(bool fail) throws Failure {
    throw (fail == true) Failure { .value = new i32(73) };
    return new i32(43);
}
void checked_void_worker(bool fail) throws Failure {
    throw (fail == true) Failure { .value = new i32(74) };
}
bool report_ok(const std.thread::panic_report* report) {
    constexpr str category = std.thread::panic_category(report);
    str text = std.thread::panic_text(report);
    text as void;
    return std.bytes::equal(category, "explicit") && std.bytes::equal(text, "simulated worker panic");
}
i32 sync_suite(bool simulated) throws std.thread::thread_error, Failure {
    std.thread::join_handle<i32> copy_handle = std.thread::spawn(copy_worker);
    std.thread::join_result<i32> copy_result = std.thread::join(move copy_handle);
    switch (move copy_result) {
    case variant std.thread::join_result::returned(move value):
        if (simulated == true || value != 41) { return 1; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 2; } break;
    }
    std.thread::join_handle<own i32*> owner_handle = std.thread::spawn(owner_worker);
    std.thread::join_result<own i32*> owner_result = std.thread::join(move owner_handle);
    switch (move owner_result) {
    case variant std.thread::join_result::returned(move value):
        if (simulated == true || *value != 42) { return 3; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 4; } break;
    }
    std.thread::join_handle<void> void_handle = std.thread::spawn(void_worker);
    std.thread::join_result<void> void_result = std.thread::join(move void_handle);
    switch (move void_result) {
    case variant std.thread::join_result::completed:
        if (simulated == true) { return 5; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 6; } break;
    }
    std.thread::join_handle<own i32* throws Failure> checked_handle = std.thread::spawn(checked_worker, false);
    std.thread::join_result<own i32*> checked_result = std.thread::join(move checked_handle);
    switch (move checked_result) {
    case variant std.thread::join_result::returned(move value):
        if (simulated == true || *value != 43) { return 7; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 8; } break;
    }
    std.thread::join_handle<void throws Failure> checked_void = std.thread::spawn(checked_void_worker, false);
    std.thread::join_result<void> checked_done = std.thread::join(move checked_void);
    switch (move checked_done) {
    case variant std.thread::join_result::completed:
        if (simulated == true) { return 9; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 10; } break;
    }
    i32 observed = 0;
    try {
        std.thread::join_handle<own i32* throws Failure> failure_handle = std.thread::spawn(checked_worker, true);
        std.thread::join_result<own i32*> failed = std.thread::join(move failure_handle);
        switch (move failed) {
        case variant std.thread::join_result::returned(move value): return 11;
        case variant std.thread::join_result::panicked(move report):
            if (simulated == false || report_ok(&report) == false) { return 12; } break;
        }
    } catch (Failure failure) { observed = *failure.value; }
    if (observed != (simulated == true ? 0 : 73)) { return 13; }
    try {
        std.thread::join_handle<void throws Failure> failure_handle = std.thread::spawn(checked_void_worker, true);
        std.thread::join_result<void> failed = std.thread::join(move failure_handle);
        switch (move failed) {
        case variant std.thread::join_result::completed: return 14;
        case variant std.thread::join_result::panicked(move report):
            if (simulated == false || report_ok(&report) == false) { return 15; } break;
        }
    } catch (Failure failure) { observed = *failure.value; }
    if (observed != (simulated == true ? 0 : 74)) { return 16; }
    std.thread::join_handle<own i32*> unobserved = std.thread::spawn(owner_worker);
    std.thread::join_result<own i32*> ignored = std.thread::join(move unobserved);
    drop ignored;
    return 0;
}
async i32 async_suite(bool simulated) throws std.thread::thread_error, Failure, std.async::start_error, std.time::time_error {
    std.thread::join_handle<i32> copy_handle = std.thread::spawn(copy_worker);
    std.thread::join_result<i32> copy_result = std.thread::join(move copy_handle);
    switch (move copy_result) {
    case variant std.thread::join_result::returned(move value):
        if (simulated == true || value != 41) { return 1; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 2; } break;
    }
    std.thread::join_handle<own i32*> owner_handle = std.thread::spawn(owner_worker);
    std.thread::join_result<own i32*> owner_result = std.thread::join(move owner_handle);
    await std.time::sleep_for(std.time::duration_from_seconds(0i64));
    switch (move owner_result) {
    case variant std.thread::join_result::returned(move value):
        if (simulated == true || *value != 42) { return 3; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 4; } break;
    }
    std.thread::join_handle<void> void_handle = std.thread::spawn(void_worker);
    std.thread::join_result<void> void_result = std.thread::join(move void_handle);
    switch (move void_result) {
    case variant std.thread::join_result::completed:
        if (simulated == true) { return 5; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 6; } break;
    }
    std.thread::join_handle<own i32* throws Failure> checked_handle = std.thread::spawn(checked_worker, false);
    std.thread::join_result<own i32*> checked_result = std.thread::join(move checked_handle);
    switch (move checked_result) {
    case variant std.thread::join_result::returned(move value):
        if (simulated == true || *value != 43) { return 7; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 8; } break;
    }
    std.thread::join_handle<void throws Failure> checked_void = std.thread::spawn(checked_void_worker, false);
    std.thread::join_result<void> checked_done = std.thread::join(move checked_void);
    switch (move checked_done) {
    case variant std.thread::join_result::completed:
        if (simulated == true) { return 9; } break;
    case variant std.thread::join_result::panicked(move report):
        if (simulated == false || report_ok(&report) == false) { return 10; } break;
    }
    i32 observed = 0;
    try {
        std.thread::join_handle<own i32* throws Failure> failure_handle = std.thread::spawn(checked_worker, true);
        std.thread::join_result<own i32*> failed = std.thread::join(move failure_handle);
        switch (move failed) {
        case variant std.thread::join_result::returned(move value): return 11;
        case variant std.thread::join_result::panicked(move report):
            if (simulated == false || report_ok(&report) == false) { return 12; } break;
        }
    } catch (Failure failure) { observed = *failure.value; }
    if (observed != (simulated == true ? 0 : 73)) { return 13; }
    try {
        std.thread::join_handle<void throws Failure> failure_handle = std.thread::spawn(checked_void_worker, true);
        std.thread::join_result<void> failed = std.thread::join(move failure_handle);
        switch (move failed) {
        case variant std.thread::join_result::completed: return 14;
        case variant std.thread::join_result::panicked(move report):
            if (simulated == false || report_ok(&report) == false) { return 15; } break;
        }
    } catch (Failure failure) { observed = *failure.value; }
    if (observed != (simulated == true ? 0 : 74)) { return 16; }
    std.thread::join_handle<own i32*> unobserved = std.thread::spawn(owner_worker);
    std.thread::join_result<own i32*> ignored = std.thread::join(move unobserved);
    drop ignored;
    return 0;
}
async i32 main(const str[] arguments) {
    try {
        bool simulated = len(arguments) == 2usize;
        try {
            i32 sync_status = sync_suite(simulated);
            if (sync_status != 0) { return sync_status; }
            i32 async_status = await async_suite(simulated);
            return async_status;
        } catch (std.thread::thread_error failure) { throw TestAssertionFailed {.code = 31}; }
        catch (Failure failure) { throw TestAssertionFailed {.code = 32}; }
        catch (std.time::time_error failure) { throw TestAssertionFailed {.code = 34}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 33}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
