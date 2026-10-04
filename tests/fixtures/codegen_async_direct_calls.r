module test.codegen.async_direct_calls;
import std.alloc;

/* P4.4: an awaited call of an async function whose body cannot suspend runs that body directly
   on the awaiting task while the runtime allows it (Core R-AM-0003). The direct path keeps what
   a started task shows: results, checked errors, task identifiers in start order
   (R-SLIB-ASYNC-0018), the refusal of a start under a budget (R-STMT-0020) and the request to
   cancel the awaiting task. An awaited std.sync receive whose channel holds a value, or has lost
   every sender, completes at once the same way. A line marked `direct` or `started` states
   whether the C17 output runs its await without the task (tests/check_direct_calls.py). */

error Rejected { u32 value; };

struct Pair { u32 low; u32 high; };

struct Gate { atomic u32 entered; };

async u32 mix(u32 left, u32 right) {
    return (left ^ right) * 31u32 + 7u32;
}

async void nothing(u32 value) {
    value as void;
}

async Pair split(u32 value) {
    return Pair {.low = value & 65535u32, .high = value >> 16u32};
}

async u64 current() {
    return std.async::task_id();
}

async u32 checked(u32 value) throws Rejected {
    throw (value > 100u32) Rejected {.value = value};
    return value + 1u32;
}

// Awaits inside, so it keeps its task; its own await of mix runs directly.
async u32 nested(u32 value) throws std.async::start_error {
    u32 inner = await mix(value, 1u32); /* direct */
    return inner + 1u32;
}

// A string argument moves into the frame of a started task.
async usize measure(std.string::string text) {
    return text.len();
}

// A deadline block narrows the deadline of the running task.
async u32 bounded(std.time::instant limit, u32 value) {
    u32 result = value;
    deadline (limit) {
        result += 1u32;
    }
    return result;
}

@scoped
async u32 borrowed(const u32* value) throws std.async::start_error {
    return *value + 1u32;
}

std.time::duration micros(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts(0i64, count * 1000u32);
}

// Waits until the spinning task has entered, yielding to other tasks.
async bool reaches(arc Gate gate)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    for (i32 attempt = 0; attempt < 2000; attempt += 1) {
        {
            const Gate* shared = &*gate;
            if (core::atomic_load(&shared->entered, core::memory_order::acquire) != 0u32) {
                return true;
            }
        }
        await std.time::sleep_for(micros(1000u32));
    }
    return false;
}

// Spins on direct calls until the request to cancel its task reaches one of their awaits.
async u32 spin(arc Gate gate) throws std.async::start_error {
    {
        const Gate* shared = &*gate;
        core::atomic_fetch_add(&shared->entered, 1u32, core::memory_order::acq_rel) as void;
    }
    u32 value = 1u32;
    for (u64 round = 0u64; round < 4611686018427387904u64; round += 1u64) {
        value = await mix(value, 3u32); /* direct */
    }
    return value;
}

// 1. Results of every shape reach the awaiting task; calls that need a task keep it.
async i32 results()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    i32 status = 0;
    if ((await mix(3u32, 5u32)) != 193u32) { status += 1; } /* direct */
    await nothing(7u32); /* direct */
    Pair pair = await split(74565u32); /* direct */
    if ((pair.low != 9029u32) || (pair.high != 1u32)) { status += 2; }
    if ((await nested(4u32)) != 163u32) { status += 4; } /* started */
    std.string::string text = std.string::from_str("direct");
    if ((await measure(move text)) != 6usize) { status += 8; } /* started */
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit = now.add(std.time::duration_from_seconds(60i64));
    if ((await bounded(limit, 9u32)) != 10u32) { status += 16; } /* started */
    task<u32> later = mix(1u32, 1u32); /* started */
    if ((await move later) != 7u32) { status += 32; }
    return status;
}

// 2. A checked error of the body reaches the awaiting task.
async i32 errors() throws std.async::start_error {
    i32 status = 0;
    try {
        if ((await checked(5u32)) != 6u32) { status += 1; } /* direct */
        u32 refused = await checked(500u32); /* direct */
        refused as void;
        status += 2;
    } catch (Rejected failure) {
        if (failure.value != 500u32) { status += 4; }
    }
    return status;
}

// 3. Direct calls take their identifiers in start order, as started tasks do.
async i32 identifiers() throws std.async::start_error {
    i32 status = 0;
    u64 caller = std.async::task_id();
    u64 first = await current(); /* direct */
    task<u64> pending = current(); /* started */
    u64 second = await move pending;
    u64 third = await current(); /* direct */
    if (first <= caller) { status += 1; }
    if (second != first + 1u64) { status += 2; }
    if (third != second + 1u64) { status += 4; }
    if (std.async::task_id() != caller) { status += 8; }
    return status;
}

// 4. Under a budget the call starts as a task, which the task limit refuses.
async i32 budgeted() throws std.error::fault {
    i32 status = 0;
    budget (std.alloc::limits {.tasks = o::some(0usize)}) {
        try {
            u32 value = await mix(1u32, 2u32); /* direct */
            value as void;
            status += 1;
        } catch (std.async::start_error failure) {
            if (failure != std.async::start_error::budget_exhausted) { status += 2; }
        }
    }
    return status;
}

// 5. A request to cancel a task that spins on direct calls reaches the await of one of them.
async i32 cancellation()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    arc Gate gate = new arc Gate {.entered = 0u32};
    i32 status = 0;
    task_scope(2) group {
        auto spinner = spin(std.arc::clone(&gate)); /* started */
        if ((await reaches(std.arc::clone(&gate))) == false) { status += 1; } /* started */
        std.async::cancel(move spinner);
    }
    return status;
}

// 6. A scoped call keeps its task in the group.
async i32 scoped() throws std.async::start_error {
    u32 value = 41u32;
    u32 result = 0u32;
    task_scope(1) group {
        result += await borrowed(&value); /* started */
    }
    if (result != 42u32) { return 1; }
    return 0;
}

// Sends one value from a task of its own.
async void deliver(std.sync::sender<u32> sender, u32 value) {
    std.sync::send_result<u32> result = std.sync::send(&sender, value);
    switch (move result) {
    case variant std.sync::send_result::sent: break;
    case variant std.sync::send_result::disconnected(move lost): lost as void;
    case variant std.sync::send_result::allocation_failed(move lost): lost as void;
    }
}

u32 received(o<u32> next) {
    switch (next) {
    case variant o::some(value): return *value;
    case variant o::none: return 0u32;
    }
}

// 7. A receive from a channel that holds a value, or has lost every sender, completes at once;
// from an empty channel it waits for the sender.
async i32 channels() throws std.alloc::alloc_error, std.async::start_error {
    i32 status = 0;
    std.sync::channel<u32> factory = std.sync::channel::<u32>();
    std.sync::sender<u32> sender = std.sync::sender(&factory);
    std.sync::receiver<u32> inbox = std.sync::receiver(move factory);
    await deliver(std.sync::clone_sender(&sender), 5u32); /* started */
    await deliver(std.sync::clone_sender(&sender), 6u32); /* started */
    if (received(await inbox.receive()) != 5u32) { status += 1; } /* direct */
    if (received(await inbox.receive()) != 6u32) { status += 2; } /* direct */
    task<void> later = deliver(std.sync::clone_sender(&sender), 7u32); /* started */
    if (received(await inbox.receive()) != 7u32) { status += 4; } /* direct */
    await move later;
    drop sender;
    o<u32> closed = await inbox.receive(); /* direct */
    switch (closed) {
    case variant o::some(value): status += 8;
    case variant o::none: break;
    }
    return status;
}

// Awaited only inside a task scope.
async u32 grouped(u32 value) {
    return value * 2u32;
}

// 8. An awaited call inside a task scope joins its group and keeps its task (P4.4-7).
async i32 in_group() throws std.async::start_error {
    u32 result = 0u32;
    task_scope(1) group {
        result += await grouped(21u32); /* started */
    }
    if (result != 42u32) { return 1; }
    return 0;
}

async i32 main() {
    i32 first = await results(); /* started */
    if (first != 0) { return first; }
    i32 second = await errors(); /* started */
    if (second != 0) { return 64 + second; }
    i32 third = await identifiers(); /* started */
    if (third != 0) { return 72 + third; }
    i32 fourth = await budgeted(); /* started */
    if (fourth != 0) { return 88 + fourth; }
    i32 fifth = await cancellation(); /* started */
    if (fifth != 0) { return 92 + fifth; }
    i32 sixth = await scoped(); /* started */
    if (sixth != 0) { return 96 + sixth; }
    i32 seventh = await channels(); /* started */
    if (seventh != 0) { return 100 + seventh; }
    i32 eighth = await in_group(); /* started */
    if (eighth != 0) { return 108 + eighth; }
    return 0;
}
