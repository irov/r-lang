module test.codegen.async_reserve;

/* R-LIB-0016 (L30): a bounded std.sync channel reserves a slot before the value exists.
   reserve waits for a free slot, try_reserve reports full at once, a permit sends into its own
   slot and never blocks, a dropped permit frees its slot, and a reservation fails with
   disconnected once the receiver is gone; a cancelled reserve holds no slot. */

std.time::duration micros(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts(0i64, count * 1000u32);
}

o<std.sync::permit<i32>> taken(std.sync::try_reserve_result<i32> result) {
    switch (move result) {
    case variant std.sync::try_reserve_result::reserved(move permit): return o::some(move permit);
    case variant std.sync::try_reserve_result::full: return o::none;
    case variant std.sync::try_reserve_result::disconnected: return o::none;
    }
}

bool full(std.sync::try_reserve_result<i32> result) {
    switch (move result) {
    case variant std.sync::try_reserve_result::reserved(move permit): drop permit; return false;
    case variant std.sync::try_reserve_result::full: return true;
    case variant std.sync::try_reserve_result::disconnected: return false;
    }
}

i32 value_or(o<i32> received, i32 fallback) {
    switch (received) {
    case variant o::some(value): return *value;
    case variant o::none: return fallback;
    }
}

// Sends through a permit when the reservation succeeded.
i32 send_through(o<std.sync::permit<i32>> slot, i32 value) {
    switch (move slot) {
    case variant o::some(move permit): (move permit).send(value); return 0;
    case variant o::none: return 1;
    }
}

// 1. Two slots are reserved at once, a third try_reserve is full, a dropped permit frees its
// slot, and values arrive in the order the permits send them.
async i32 reserved() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::sync_channel<i32> factory = std.sync::sync_channel::<i32>(2usize);
    std.sync::sync_sender<i32> tx = std.sync::sync_sender(&factory);
    std.sync::receiver<i32> rx = std.sync::sync_receiver(move factory);
    o<std.sync::permit<i32>> first = taken(tx.try_reserve());
    o<std.sync::permit<i32>> second = taken(tx.try_reserve());
    i32 status = full(tx.try_reserve()) == true ? 0 : 1;
    status += send_through(move second, 20) * 2;
    drop first;
    o<std.sync::permit<i32>> third = taken(tx.try_reserve());
    status += send_through(move third, 30) * 4;
    if (value_or(await rx.receive(), 0) != 20) { status += 8; }
    if (value_or(await rx.receive(), 0) != 30) { status += 16; }
    return status;
}

// Sends 1..count, reserving each slot before the value exists.
async i32 produce(std.sync::sync_sender<i32> tx, i32 count) throws std.async::start_error {
    i32 status = 0;
    for (i32 value = 1; value <= count; value += 1) {
        std.sync::reserve_result<i32> slot = await tx.reserve();
        switch (move slot) {
        case variant std.sync::reserve_result::reserved(move permit): (move permit).send(value);
        case variant std.sync::reserve_result::disconnected: status = 1;
        }
    }
    return status;
}

// 2. A producer faster than its consumer waits on reserve; every value arrives in order.
async i32 backpressure()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.sync::sync_channel<i32> factory = std.sync::sync_channel::<i32>(4usize);
    std.sync::sync_sender<i32> tx = std.sync::sync_sender(&factory);
    std.sync::receiver<i32> rx = std.sync::sync_receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto producer = produce(move tx, 1000);
        i32 expected = 1;
        bool open = true;
        while (open == true) {
            o<i32> next = await rx.receive();
            switch (next) {
            case variant o::some(value):
                if (*value != expected) { status |= 1; }
                expected += 1;
            case variant o::none: open = false;
            }
            if (expected % 250 == 0) {
                await std.time::sleep_for(micros(1000u32));
            }
        }
        (move rx) as void;
        if (expected != 1001) { status |= 2; }
        status += (await move producer) * 4;
    }
    return status;
}

// 3. A reserve waiting on a full channel ends with disconnected when the receiver is dropped,
// and later reservations fail at once.
async i32 disconnected() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::sync_channel<i32> factory = std.sync::sync_channel::<i32>(1usize);
    std.sync::sync_sender<i32> tx = std.sync::sync_sender(&factory);
    std.sync::receiver<i32> rx = std.sync::sync_receiver(move factory);
    i32 status = send_through(taken(tx.try_reserve()), 1);
    task<std.sync::reserve_result<i32>> waiting = tx.reserve();
    drop rx;
    std.sync::reserve_result<i32> ended = await move waiting;
    switch (move ended) {
    case variant std.sync::reserve_result::reserved(move permit): drop permit; status += 2;
    case variant std.sync::reserve_result::disconnected: break;
    }
    std.sync::try_reserve_result<i32> after = tx.try_reserve();
    switch (move after) {
    case variant std.sync::try_reserve_result::reserved(move permit): drop permit; status += 4;
    case variant std.sync::try_reserve_result::full: status += 8;
    case variant std.sync::try_reserve_result::disconnected: break;
    }
    return status;
}

// 4. A reserve that loses a select to a timer is cancelled and holds no slot.
async i32 cancelled()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.sync::sync_channel<i32> factory = std.sync::sync_channel::<i32>(1usize);
    std.sync::sync_sender<i32> tx = std.sync::sync_sender(&factory);
    std.sync::receiver<i32> rx = std.sync::sync_receiver(move factory);
    i32 status = send_through(taken(tx.try_reserve()), 7);
    task_scope(2) group {
        auto waiter = tx.reserve();
        auto tick = std.time::sleep_for(micros(20000u32));
        select (group) {
        case std.sync::reserve_result<i32> early = await move waiter:
            (move early) as void;
            status += 2;
            break;
        case await move tick: break;
        }
        group.cancel_all();
        await group.all();
    }
    if (value_or(await rx.receive(), 0) != 7) { status += 4; }
    status += send_through(taken(tx.try_reserve()), 8) * 8;
    if (full(tx.try_reserve()) == false) { status += 16; }
    if (value_or(await rx.receive(), 0) != 8) { status += 32; }
    return status;
}

async i32 main() {
    // The status names the first section that failed.
    try {
        if ((await reserved()) != 0) { return 1; }
        if ((await backpressure()) != 0) { return 2; }
        if ((await disconnected()) != 0) { return 3; }
        if ((await cancelled()) != 0) { return 4; }
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
