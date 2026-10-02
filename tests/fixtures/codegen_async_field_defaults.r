module test.codegen.async_field_defaults;

/* R-INIT-0004, R-INIT-0005 (L33): field initializers in async functions. The initializer is
   evaluated in the frame of the async function that initializes the struct; a value it
   produces lives across suspension points, and a checked error it throws is an error of the
   async function. */

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = std.string::as_bytes(source);
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

/* The counter is shared by every thread: the steps of an async function may run on different
   executor threads, each with its own thread_local instance (R-OBJ-0008). */
atomic u32 ticket = 0u32;

u32 next_ticket() {
    return core::atomic_fetch_add(&ticket, 1u32, core::memory_order::relaxed) + 1u32;
}

enum State { idle, @default ready, busy };

struct Job {
    std.string::string label = std.string::from_str("job");
    u32 order = next_ticket();
    State state;
    u32 attempts = 3;
};

async u32 pause(u32 value) { return value; }

async i32 build() throws std.alloc::alloc_error, std.async::start_error {
    core::atomic_store(&ticket, 0u32, core::memory_order::relaxed);
    i32 status = 0;
    Job first = Job {};
    u32 waited = await pause(first.order);
    if ((matches(&first.label, "job") == false) || (first.order != 1u32)) { status += 1; }
    Job second = Job {.attempts = waited + 10u32};
    if ((second.order != 2u32) || (second.attempts != 11u32) || (second.state != State::ready)) {
        status += 2;
    }
    u32 later = await pause(4u32);
    Job[2] pair = {};
    u32 second_order = pair[1].order;
    if ((second_order != later) || (pair[0].order != 3u32)) { status += 4; }
    Job taken = core::take(&second);
    if ((taken.order != 2u32) || (second.order != 5u32)) { status += 8; }
    drop first;
    drop second;
    drop pair;
    drop taken;
    return status;
}

async i32 main() {
    try {
        return await build();
    } catch (std.alloc::alloc_error failure) {
        return 90;
    } catch (std.async::start_error failure) {
        return 91;
    }
}
