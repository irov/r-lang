module test.codegen.async_blocking;

/* R-SLIB-ASYNC-0017 (L31): std.async::blocking runs a direct entry once on the blocking call pool
   and completes its task with the entry's result or checked error. The pool runs at most four
   calls at once and takes the rest in submission order; cancellation removes a call that no pool
   thread took, and a taken call runs to its return before the task reports cancellation. */

error Rejected { i64 value; };

struct Gate { atomic u32 open; atomic u32 entered; atomic u32 finished; };

i64 square(i64 value) {
    return value * value;
}

usize measure(std.string::string text) {
    return text.len();
}

i64 checked(i64 value) throws Rejected {
    throw (value < 0i64) Rejected {.value = value};
    return value + 1i64;
}

@generic<T>
T same(T value) {
    return move value;
}

void touch(arc Gate gate) {
    const Gate* shared = &*gate;
    core::atomic_fetch_add(&shared->finished, 1u32, core::memory_order::acq_rel) as void;
}

// Blocks its pool thread until the gate opens, as a blocking C call would.
u32 hold(arc Gate gate) {
    const Gate* shared = &*gate;
    core::atomic_fetch_add(&shared->entered, 1u32, core::memory_order::acq_rel) as void;
    while (core::atomic_load(&shared->open, core::memory_order::acquire) == 0u32) {
        std.thread::yield_now();
    }
    return core::atomic_fetch_add(&shared->finished, 1u32, core::memory_order::acq_rel) + 1u32;
}

// A synchronous function starts the call; its caller awaits the task.
task<i64> start_square(i64 value) throws std.async::start_error {
    return std.async::blocking(square, value);
}

std.time::duration micros(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts(0i64, count * 1000u32);
}

// Waits until the counter reaches the value, yielding to other tasks.
async bool reaches(arc Gate gate, u32 expected)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    for (i32 attempt = 0; attempt < 2000; attempt += 1) {
        {
            const Gate* shared = &*gate;
            if (core::atomic_load(&shared->entered, core::memory_order::acquire) >= expected) {
                return true;
            }
        }
        await std.time::sleep_for(micros(1000u32));
    }
    return false;
}

void open(const Gate* shared) {
    core::atomic_store(&shared->open, 1u32, core::memory_order::release);
}

// 1. Values, void, owned arguments, a generic entry and a checked error reach the awaiting task.
async i32 values() throws std.alloc::alloc_error, std.async::start_error {
    i32 status = 0;
    if ((await std.async::blocking(square, 9i64)) != 81i64) { status += 1; }
    std.string::string text = std.string::from_str("pool");
    if ((await std.async::blocking(measure, move text)) != 4usize) { status += 2; }
    if ((await std.async::blocking(same, 7u16)) != 7u16) { status += 4; }
    task<i64> started = start_square(12i64);
    if ((await move started) != 144i64) { status += 128; }
    arc Gate gate = new arc Gate {.open = 1u32, .entered = 0u32, .finished = 0u32};
    arc Gate toucher = std.arc::clone(&gate);
    await std.async::blocking(touch, move toucher);
    {
        const Gate* shared = &*gate;
        if (core::atomic_load(&shared->finished, core::memory_order::acquire) != 1u32) {
            status += 8;
        }
    }
    try {
        i64 accepted = await std.async::blocking(checked, 5i64);
        if (accepted != 6i64) { status += 16; }
        i64 refused = await std.async::blocking(checked, -3i64);
        refused as void;
        status += 32;
    } catch (Rejected failure) {
        if (failure.value != -3i64) { status += 64; }
    }
    return status;
}

// 2. Four held calls occupy the pool; a fifth waits in the queue until one returns.
async i32 exhaustion()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    arc Gate gate = new arc Gate {.open = 0u32, .entered = 0u32, .finished = 0u32};
    i32 status = 0;
    task_scope(7) group {
        arc Gate a = std.arc::clone(&gate);
        arc Gate b = std.arc::clone(&gate);
        arc Gate c = std.arc::clone(&gate);
        arc Gate d = std.arc::clone(&gate);
        arc Gate e = std.arc::clone(&gate);
        auto first = std.async::blocking(hold, move a);
        auto second = std.async::blocking(hold, move b);
        auto third = std.async::blocking(hold, move c);
        auto fourth = std.async::blocking(hold, move d);
        if ((await reaches(std.arc::clone(&gate), 4u32)) == false) { status += 1; }
        auto fifth = std.async::blocking(hold, move e);
        await std.time::sleep_for(micros(20000u32));
        {
            const Gate* shared = &*gate;
            if (core::atomic_load(&shared->entered, core::memory_order::acquire) != 4u32) {
                status += 2;
            }
            open(shared);
        }
        u32 total = (await move first) + (await move second) + (await move third) +
                    (await move fourth) + (await move fifth);
        if (total != 15u32) { status += 4; }
    }
    return status;
}

// 3. A queued call is removed by cancellation and never enters; its argument is destroyed.
async i32 cancelled_queued()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    arc Gate gate = new arc Gate {.open = 0u32, .entered = 0u32, .finished = 0u32};
    arc Gate spare = new arc Gate {.open = 1u32, .entered = 0u32, .finished = 0u32};
    i32 status = 0;
    task_scope(6) group {
        arc Gate a = std.arc::clone(&gate);
        arc Gate b = std.arc::clone(&gate);
        arc Gate c = std.arc::clone(&gate);
        arc Gate d = std.arc::clone(&gate);
        arc Gate waiting = std.arc::clone(&spare);
        auto first = std.async::blocking(hold, move a);
        auto second = std.async::blocking(hold, move b);
        auto third = std.async::blocking(hold, move c);
        auto fourth = std.async::blocking(hold, move d);
        if ((await reaches(std.arc::clone(&gate), 4u32)) == false) { status += 1; }
        auto queued = std.async::blocking(hold, move waiting);
        std.async::cancel(move queued);
        {
            const Gate* shared = &*gate;
            open(shared);
        }
        u32 total = (await move first) + (await move second) + (await move third) +
                    (await move fourth);
        if (total != 10u32) { status += 2; }
    }
    if (std.arc::strong_count(&spare) != 1usize) { status += 4; }
    const Gate* shared = &*spare;
    if (core::atomic_load(&shared->entered, core::memory_order::acquire) != 0u32) { status += 8; }
    return status;
}

// 4. A taken call that loses a select to a timer is not interrupted: the scope waits for its
// return, and its result is destroyed.
async i32 cancelled_taken()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    arc Gate gate = new arc Gate {.open = 0u32, .entered = 0u32, .finished = 0u32};
    i32 status = 0;
    task_scope(3) group {
        arc Gate holder = std.arc::clone(&gate);
        auto held = std.async::blocking(hold, move holder);
        if ((await reaches(std.arc::clone(&gate), 1u32)) == false) { status += 1; }
        auto tick = std.time::sleep_for(micros(20000u32));
        select (group) {
        case u32 early = await move held: early as void; status += 2; break;
        case await move tick: break;
        }
        group.cancel_all();
        {
            const Gate* shared = &*gate;
            open(shared);
        }
        await group.all();
    }
    const Gate* shared = &*gate;
    if (core::atomic_load(&shared->finished, core::memory_order::acquire) != 1u32) { status += 4; }
    if (std.arc::strong_count(&gate) != 1usize) { status += 8; }
    return status;
}

async i32 main() {
    // The status names the first section that failed.
    try {
        if ((await values()) != 0) { return 1; }
        if ((await exhaustion()) != 0) { return 2; }
        if ((await cancelled_queued()) != 0) { return 3; }
        if ((await cancelled_taken()) != 0) { return 4; }
    } catch (std.alloc::alloc_error failure) {
        return 101;
    } catch (std.time::time_error failure) {
        return 102;
    } catch (std.time::duration_error failure) {
        return 103;
    } catch (std.async::start_error failure) {
        return 104;
    }
    return 0;
}
