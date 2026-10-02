module test.codegen.async_locks;

/* R-SLIB-ASYNC-0013..0015 (L30): std.async::mutex, rw_lock, semaphore and notify. A guard or a
   permit owns a reference to its lock, so it may stay live across await; waiters are served in
   start order and a free lock is handed to the first of them; try_* fails while waiters queue;
   a cancelled waiter never takes the resource with it. */

struct Tally { u64 total; u64 rounds; };
struct Load { atomic u32 active; atomic u32 crowded; atomic u32 runs; };

std.time::duration micros(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts(0i64, count * 1000u32);
}

// Holds the guard across a real suspension on every round.
async void bump(std.async::mutex<Tally> shared, i32 rounds)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    for (i32 round = 0; round < rounds; round += 1) {
        std.async::mutex_guard<Tally> guard = await shared.lock();
        u64 before = (guard.get())->total;
        await std.time::sleep_for(micros(50u32));
        (guard.get_mut())->total = before + 1u64;
        (guard.get_mut())->rounds += 1u64;
        (move guard).unlock();
    }
}

// 1. Four tasks update one value under the lock with an await inside the critical section; no
// update is lost.
async i32 counted()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.async::mutex<Tally> shared = std.async::mutex_new(Tally {.total = 0u64, .rounds = 0u64});
    task_scope(4) group {
        for (i32 index = 0; index < 4; index += 1) {
            auto member = bump(shared.clone(), 25);
            std.async::detach(move member);
        }
        await group.all();
    }
    std.async::mutex_guard<Tally> guard = await shared.lock();
    i32 status = 0;
    if ((guard.get())->total != 100u64) { status += 1; }
    if ((guard.get())->rounds != 100u64) { status += 2; }
    (move guard).unlock();
    return status;
}

i32 absent(o<std.async::mutex_guard<i32>> attempt) {
    switch (move attempt) {
    case variant o::some(move guard): (move guard).unlock(); return 1;
    case variant o::none: return 0;
    }
}

// 2. Waiters are served in start order; try_lock fails while the lock is held or handed over.
async i32 ordered() throws std.alloc::alloc_error, std.async::start_error {
    std.async::mutex<i32> m = std.async::mutex_new(0);
    std.async::mutex_guard<i32> held = await m.lock();
    task<std.async::mutex_guard<i32>> first = m.lock();
    task<std.async::mutex_guard<i32>> second = m.lock();
    i32 status = absent(m.try_lock());
    (move held).unlock();
    status += absent(m.try_lock()) * 2;
    std.async::mutex_guard<i32> one = await move first;
    *(one.get_mut()) = 1;
    (move one).unlock();
    std.async::mutex_guard<i32> two = await move second;
    if (*(two.get()) != 1) { status += 4; }
    *(two.get_mut()) = 2;
    (move two).unlock();
    o<std.async::mutex_guard<i32>> last = m.try_lock();
    switch (move last) {
    case variant o::some(move guard):
        if (*(guard.get()) != 2) { status += 8; }
        (move guard).unlock();
    case variant o::none: status += 16;
    }
    return status;
}

// 3. A waiter that loses a select to a timer is cancelled and leaves the lock free.
async i32 cancelled()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.async::mutex<i32> m = std.async::mutex_new(5);
    std.async::mutex_guard<i32> held = await m.lock();
    i32 status = 0;
    task_scope(2) group {
        auto waiter = m.lock();
        auto tick = std.time::sleep_for(micros(20000u32));
        select (group) {
        case std.async::mutex_guard<i32> got = await move waiter:
            (move got).unlock();
            status += 1;
            break;
        case await move tick: break;
        }
        group.cancel_all();
        await group.all();
    }
    (move held).unlock();
    o<std.async::mutex_guard<i32>> free = m.try_lock();
    switch (move free) {
    case variant o::some(move guard):
        if (*(guard.get()) != 5) { status += 2; }
        (move guard).unlock();
    case variant o::none: status += 4;
    }
    return status;
}

i32 missing_read(o<std.async::rw_read_guard<i32>> attempt) {
    switch (move attempt) {
    case variant o::some(move guard): (move guard).unlock(); return 0;
    case variant o::none: return 1;
    }
}

i32 missing_write(o<std.async::rw_write_guard<i32>> attempt) {
    switch (move attempt) {
    case variant o::some(move guard): (move guard).unlock(); return 0;
    case variant o::none: return 1;
    }
}

// 4. Readers share the lock, a writer excludes them, and a queued writer stops new readers.
async i32 shared_reads() throws std.alloc::alloc_error, std.async::start_error {
    std.async::rw_lock<i32> table = std.async::rwlock_new(7);
    std.async::rw_lock<i32> view = table.clone();
    std.async::rw_read_guard<i32> reader = await table.read();
    i32 status = missing_read(view.try_read());
    status += (1 - missing_write(view.try_write())) * 2;
    task<std.async::rw_write_guard<i32>> writer = view.write();
    status += (1 - missing_read(table.try_read())) * 4;
    if (*(reader.get()) != 7) { status += 8; }
    (move reader).unlock();
    std.async::rw_write_guard<i32> exclusive = await move writer;
    if (*(exclusive.get()) != 7) { status += 16; }
    *(exclusive.get_mut()) = 9;
    (move exclusive).unlock();
    std.async::rw_read_guard<i32> after = await view.read();
    if (*(after.get()) != 9) { status += 32; }
    (move after).unlock();
    return status;
}

// Counts how many holders of a permit run at once.
async void limited(std.async::semaphore gate, arc Load load)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    std.async::semaphore_permit permit = await gate.acquire();
    {
        const Load* shared = &*load;
        u32 now = core::atomic_fetch_add(&shared->active, 1u32, core::memory_order::acq_rel) + 1u32;
        if (now > 3u32) {
            core::atomic_fetch_add(&shared->crowded, 1u32, core::memory_order::acq_rel) as void;
        }
    }
    await std.time::sleep_for(micros(2000u32));
    const Load* shared = &*load;
    core::atomic_fetch_sub(&shared->active, 1u32, core::memory_order::acq_rel) as void;
    core::atomic_fetch_add(&shared->runs, 1u32, core::memory_order::acq_rel) as void;
    (move permit).release();
}

i32 missing_permit(o<std.async::semaphore_permit> attempt) {
    switch (move attempt) {
    case variant o::some(move permit): (move permit).release(); return 0;
    case variant o::none: return 1;
    }
}

// 5. A semaphore bounds concurrency, and add_permits raises the bound.
async i32 bounded()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.async::semaphore gate = std.async::semaphore_new(2usize);
    std.async::semaphore_permit a = await gate.acquire();
    std.async::semaphore_permit b = await gate.acquire();
    i32 status = missing_permit(gate.try_acquire()) == 1 ? 0 : 1;
    if (gate.available_permits() != 0usize) { status += 2; }
    gate.add_permits(1usize);
    if (gate.available_permits() != 1usize) { status += 4; }
    (move a).release();
    (move b).release();
    if (gate.available_permits() != 3usize) { status += 8; }
    arc Load load = new arc Load {.active = 0u32, .crowded = 0u32, .runs = 0u32};
    task_scope(6) group {
        for (i32 index = 0; index < 6; index += 1) {
            auto member = limited(gate.clone(), std.arc::clone(&load));
            std.async::detach(move member);
        }
        await group.all();
    }
    const Load* shared = &*load;
    if (core::atomic_load(&shared->crowded, core::memory_order::acquire) != 0u32) { status += 16; }
    if (core::atomic_load(&shared->runs, core::memory_order::acquire) != 6u32) { status += 32; }
    if (gate.available_permits() != 3usize) { status += 64; }
    return status;
}

// 6. notify_one stores one permit for the next waiter; notify_all wakes every current waiter
// and stores nothing.
async i32 notified()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.async::notify bell = std.async::notify_new();
    bell.notify_one();
    await bell.notified();
    std.async::notify other = bell.clone();
    task<void> first = bell.notified();
    task<void> second = other.notified();
    task<void> third = bell.notified();
    other.notify_all();
    await move first;
    await move second;
    await move third;
    i32 status = 0;
    task_scope(2) group {
        auto waiter = bell.notified();
        auto tick = std.time::sleep_for(micros(20000u32));
        select (group) {
        case await move waiter: status += 1; break;
        case await move tick: break;
        }
        group.cancel_all();
        await group.all();
    }
    return status;
}

async i32 main() {
    // The status names the first section that failed.
    try {
        if ((await counted()) != 0) { return 1; }
        if ((await ordered()) != 0) { return 2; }
        if ((await cancelled()) != 0) { return 3; }
        if ((await shared_reads()) != 0) { return 4; }
        if ((await bounded()) != 0) { return 5; }
        if ((await notified()) != 0) { return 6; }
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
