module test.codegen.async_broadcast;

/* R-SLIB-ASYNC-0016 (L30): std.async::broadcast delivers a clone of every value to every live
   subscriber in send order; a subscriber whose bounded queue overflows loses its oldest values
   and learns how many through lagged(n) before the next value; closed follows the last value
   once every broadcast handle is gone; a cancelled receive takes no value with it. */

std.time::duration micros(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts(0i64, count * 1000u32);
}

// The length of a received value, or a negative code for lagged and closed.
i64 length_of(std.async::broadcast_result<std.string::string> next) {
    switch (move next) {
    case variant std.async::broadcast_result::received(move text): return text.len() as i64;
    case variant std.async::broadcast_result::lagged(move missed): return -(missed as i64);
    case variant std.async::broadcast_result::closed: return -1000i64;
    }
}

// 1. Every subscriber receives its own copy of every value in send order; send reports how
// many subscribers got the value; a subscriber joining later sees only later values.
async i32 fanout() throws std.alloc::alloc_error, std.async::start_error {
    std.async::broadcast<std.string::string> hub =
        std.async::broadcast::<std.string::string>(8usize);
    std.async::broadcast_receiver<std.string::string> left = hub.subscribe();
    std.async::broadcast_receiver<std.string::string> right = hub.subscribe();
    i32 status = 0;
    if (hub.send(std.string::from_str("a")) != 2usize) { status += 1; }
    if (hub.send(std.string::from_str("bb")) != 2usize) { status += 1; }
    std.async::broadcast_receiver<std.string::string> late = hub.subscribe();
    if (hub.send(std.string::from_str("ccc")) != 3usize) { status += 1; }
    for (i64 expected = 1i64; expected <= 3i64; expected += 1i64) {
        if (length_of(await left.receive()) != expected) { status += 2; }
        if (length_of(await right.receive()) != expected) { status += 4; }
    }
    if (length_of(await late.receive()) != 3i64) { status += 8; }
    (move left) as void;
    (move right) as void;
    return status;
}

// 2. A full queue drops its oldest value; lagged(n) comes first, then the newest values.
async i32 lagging() throws std.alloc::alloc_error, std.async::start_error {
    std.async::broadcast<std.string::string> hub = std.async::broadcast(4usize);
    std.async::broadcast_receiver<std.string::string> slow = hub.subscribe();
    std.string::string text = std.string::create();
    usize delivered = 0usize;
    for (i32 index = 0; index < 10; index += 1) {
        text.append("x");
        delivered += hub.send(core::clone(&text));
    }
    (move text) as void;
    i32 status = delivered == 10usize ? 0 : 1;
    if (length_of(await slow.receive()) != -6i64) { status += 2; }
    for (i64 expected = 7i64; expected <= 10i64; expected += 1i64) {
        if (length_of(await slow.receive()) != expected) { status += 4; }
    }
    return status;
}

// 3. closed comes after the queued values once every handle is gone, and stays; a send
// without subscribers reaches nobody.
async i32 closing() throws std.alloc::alloc_error, std.async::start_error {
    std.async::broadcast<std.string::string> hub = std.async::broadcast(2usize);
    i32 status = hub.send(std.string::from_str("lost")) == 0usize ? 0 : 1;
    std.async::broadcast<std.string::string> copy = hub.clone();
    std.async::broadcast_receiver<std.string::string> inbox = copy.subscribe();
    if (hub.send(std.string::from_str("kept")) != 1usize) { status += 2; }
    drop hub;
    if (copy.send(std.string::from_str("also")) != 1usize) { status += 4; }
    drop copy;
    if ((length_of(await inbox.receive())) != 4i64) { status += 8; }
    if ((length_of(await inbox.receive())) != 4i64) { status += 16; }
    if ((length_of(await inbox.receive())) != -1000i64) { status += 32; }
    if ((length_of(await inbox.receive())) != -1000i64) { status += 64; }
    return status;
}

// 4. A receive that loses a select to a timer is cancelled and takes no value with it.
async i32 cancelled()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    std.async::broadcast<std.string::string> hub = std.async::broadcast(2usize);
    std.async::broadcast_receiver<std.string::string> inbox = hub.subscribe();
    i32 status = 0;
    task_scope(2) group {
        auto waiter = inbox.receive();
        auto tick = std.time::sleep_for(micros(20000u32));
        select (group) {
        case std.async::broadcast_result<std.string::string> early = await move waiter:
            (move early) as void;
            status += 1;
            break;
        case await move tick: break;
        }
        group.cancel_all();
        await group.all();
    }
    if (hub.send(std.string::from_str("after")) != 1usize) { status += 2; }
    if ((length_of(await inbox.receive())) != 5i64) { status += 4; }
    return status;
}

// Drains a subscriber, checking that every gap in the sequence is announced by lagged(n).
async i32 drain(std.async::broadcast_receiver<i64> inbox, u64 pause_every, i64 total)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    i64 expected = 0i64;
    u64 received = 0u64;
    u64 missed = 0u64;
    i32 status = 0;
    bool open = true;
    while (open == true) {
        std.async::broadcast_result<i64> next = await inbox.receive();
        bool pause = false;
        switch (move next) {
        case variant std.async::broadcast_result::received(move value):
            if (value != expected) { status |= 1; }
            expected = value + 1i64;
            received += 1u64;
            pause = pause_every != 0u64 && received % pause_every == 0u64;
        case variant std.async::broadcast_result::lagged(move count):
            missed += count;
            expected += count as i64;
        case variant std.async::broadcast_result::closed: open = false;
        }
        if (pause == true) {
            await std.time::sleep_for(micros(1000u32));
        }
    }
    if (expected != total) { status |= 2; }
    if ((received + missed) as i64 != total) { status |= 4; }
    if (received == 0u64) { status |= 8; }
    return status;
}

// 5. 100000 values reach a fast and a slow subscriber through a queue of 64; each sees every
// value either received or counted as lagged, and the last value always arrives.
async i32 pressure()
    throws std.alloc::alloc_error, std.time::time_error, std.time::duration_error,
    std.async::start_error {
    i64 total = 100000i64;
    std.async::broadcast<i64> hub = std.async::broadcast(64usize);
    i32 status = 0;
    task_scope(3) group {
        auto fast = drain(hub.subscribe(), 0u64, total);
        auto slow = drain(hub.subscribe(), 2000u64, total);
        usize reached = 0usize;
        for (i64 value = 0i64; value < total; value += 1i64) {
            reached += hub.send(value);
            if (value % 1000i64 == 999i64) {
                await std.time::sleep_for(micros(50u32));
            }
        }
        drop hub;
        if (reached != 200000usize) { status += 1; }
        status += (await move fast) * 2;
        status += (await move slow) * 32;
    }
    return status;
}

async i32 main() {
    // The status names the first section that failed.
    try {
        if ((await fanout()) != 0) { return 1; }
        if ((await lagging()) != 0) { return 2; }
        if ((await closing()) != 0) { return 3; }
        if ((await cancelled()) != 0) { return 4; }
        if ((await pressure()) != 0) { return 5; }
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
