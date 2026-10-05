module test.codegen.async_unwind_cleanup;
import std.console;

/* L39 (Core R-ERR-0005, R-ERR-0009, Library R-SLIB-ASYNC-0020): a panic that begins three frames
   below an async task drops every live object and runs every finally on its way out, in the order
   of a normal exit; std.async::join observes it, a lock held across it is poisoned and an arc it
   held is released. */

atomic u64 trail = 0u64;

protected void mark(u64 digit) {
    u64 now = core::atomic_load(&trail, core::memory_order::relaxed);
    core::atomic_store(&trail, now * 10u64 + digit, core::memory_order::relaxed);
}

struct Probe { u64 digit; };

drop(Probe* self) {
    mark(self->digit);
}

protected i32 pick(const u8[] values, usize at) {
    return values[at] as i32;
}

protected i32 inner(usize at) {
    Probe first = {.digit = 1u64};
    u8[3] values = {1u8, 2u8, 3u8};
    try {
        return pick(values[0usize..3usize], at);
    } finally {
        mark(9u64);
    }
}

protected i32 middle(usize at) {
    Probe second = {.digit = 2u64};
    own Probe* third = new Probe {.digit = 3u64};
    i32 value = inner(at);
    return value + 1;
}

protected async i32 worker(usize at, arc Probe kept) {
    Probe fourth = {.digit = 4u64};
    i32 value = middle(at);
    drop kept;
    return value;
}

@scoped
protected async i32 hold(const std.sync::mutex<i32>* mutex, usize at) {
    std.sync::lock_result<i32> locked = std.sync::lock(mutex);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        {
            i32* value = std.sync::mutex_guard_mut(&guard);
            *value = middle(at);
        }
        std.sync::unlock(move guard);
        return 0;
    case variant std.sync::lock_result::poisoned(move guard):
        std.sync::unlock(move guard);
        return 1;
    case variant std.sync::lock_result::would_deadlock:
        return 2;
    }
}

protected i32 joined_category(std.thread::join_result<i32> joined, constexpr str expected) {
    switch (move joined) {
    case variant std.thread::join_result::returned(move value):
        return 100 + value;
    case variant std.thread::join_result::panicked(move report):
        constexpr str category = std.thread::panic_category(&report);
        if (std.bytes::equal(category, expected) == false) { return 200; }
        return 0;
    }
}

async i32 main() {
    i32 failures = 0;
    arc Probe shared = new arc Probe {.digit = 5u64};
    arc Probe lent = std.arc::clone(&shared);
    auto failing = worker(7usize, move lent);
    failures += joined_category(await std.async::join(move failing), "bounds");
    u64 unwound = core::atomic_load(&trail, core::memory_order::relaxed);
    usize owners = std.arc::strong_count(&shared);
    core::atomic_store(&trail, 0u64, core::memory_order::relaxed);
    arc Probe again = std.arc::clone(&shared);
    auto passing = worker(1usize, move again);
    i32 normal = await move passing;
    u64 returned = core::atomic_load(&trail, core::memory_order::relaxed);

    std.sync::mutex<i32> mutex = std.sync::mutex_new(0);
    i32 poisoned = 0;
    task_scope(1) group {
        auto holder = hold(&mutex, 8usize);
        failures += joined_category(await std.async::join(move holder), "bounds");
    }
    std.sync::lock_result<i32> after = std.sync::lock(&mutex);
    switch (move after) {
    case variant std.sync::lock_result::locked(move guard):
        std.sync::unlock(move guard);
    case variant std.sync::lock_result::poisoned(move guard):
        std.sync::unlock(move guard);
        poisoned = 1;
    case variant std.sync::lock_result::would_deadlock:
        failures += 1000;
    }
    await std.console::print(
        f"unwound {unwound}\nreturned {returned} value {normal}\nowners {owners}\npoisoned {poisoned}\n");
    return failures;
}
