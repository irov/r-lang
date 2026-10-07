module test.codegen.async_channels;

/* R-LIB-0016 (L24): std.sync::receive is an asynchronous operation that a task group, a select
   and a timer compose with; pending receives are served in start order, a receive cancelled
   while it waits loses no value, and the end of every sender or of the receiver completes it
   with none. */

error Stop { };

bool sent(std.sync::send_result<i32> result) {
    switch (move result) {
    case variant std.sync::send_result::sent: return true;
    case variant std.sync::send_result::disconnected(move value): return false;
    case variant std.sync::send_result::allocation_failed(move value): return false;
    }
}

i32 value_or(o<i32> received, i32 fallback) {
    switch (received) {
    case variant o::some(value): return *value;
    case variant o::none: return fallback;
    }
}

// Sends 1..count from another task, then drops its sender.
async void produce(std.sync::sender<i32> sender, i32 count) {
    for (i32 value = 1; value <= count; value += 1) {
        bool accepted = sent(std.sync::send(&sender, value));
        accepted as void;
    }
}

// 1. A consumer drains a channel filled by a concurrent producer; the end is none.
async i32 drained() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::channel<i32> factory = std.sync::channel::<i32>();
    std.sync::sender<i32> sender = std.sync::sender(&factory);
    task<void> producer = produce(move sender, 100);
    std.async::detach(move producer);
    std.sync::receiver<i32> inbox = std.sync::receiver(move factory);
    i32 total = 0;
    i32 count = 0;
    while (true) {
        o<i32> next = await inbox.receive();
        bool finished = false;
        switch (next) {
        case variant o::some(value): total += *value; count += 1; break;
        case variant o::none: finished = true; break;
        }
        if (finished == true) { break; }
    }
    if (count != 100 || total != 5050) { return 1; }
    return 0;
}

// 2. select: a timer wins against an idle channel, and the receive cancelled while it waits
// loses no value.
async i32 selected() throws std.alloc::alloc_error, std.async::start_error, std.time::time_error,
    std.time::duration_error {
    std.sync::channel<i32> factory = std.sync::channel::<i32>();
    std.sync::sender<i32> sender = std.sync::sender(&factory);
    std.sync::receiver<i32> inbox = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto message = inbox.receive();
        auto tick = std.time::sleep_for(std.time::duration_from_parts(0i64, 20000000u32));
        select (group) {
        case o<i32> received = await move message: received as void; status = 2; break;
        case await move tick: break;
        }
        group.cancel_all();
        await group.all();
    }
    bool first = sent(std.sync::send(&sender, 41));
    o<i32> after = await inbox.receive();
    if (first == false) { status += 4; }
    if (value_or(after, 0) != 41) { status += 8; }
    // The channel wins against a distant deadline.
    std.time::instant late = std.time::monotonic_now().add(std.time::duration_from_seconds(30i64));
    task_scope(1) group {
        auto message = inbox.receive();
        bool second = sent(std.sync::send(&sender, 42));
        select (group) {
        case o<i32> received = await move message:
            if (value_or(received, 0) != 42) { status += 16; }
            break;
        case until (late): status += 32; break;
        }
        if (second == false) { status += 64; }
    }
    return status;
}

// 3. Pending receives are served in start order; a receiver dropped under a pending receive
// ends it with none.
async i32 ordered() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::channel<i32> factory = std.sync::channel::<i32>();
    std.sync::sender<i32> sender = std.sync::sender(&factory);
    std.sync::receiver<i32> inbox = std.sync::receiver(move factory);
    task<o<i32>> first = inbox.receive();
    task<o<i32>> second = inbox.receive();
    bool a = sent(std.sync::send(&sender, 1));
    bool b = sent(std.sync::send(&sender, 2));
    o<i32> one = await move first;
    o<i32> two = await move second;
    task<o<i32>> orphan = inbox.receive();
    drop inbox;
    o<i32> gone = await move orphan;
    i32 status = 0;
    if (a == false) { status += 1; }
    if (b == false) { status += 1; }
    if (value_or(one, 0) != 1) { status += 2; }
    if (value_or(two, 0) != 2) { status += 4; }
    if (value_or(gone, -1) != -1) { status += 8; }
    drop sender;
    return status;
}

// 4. Owned values travel through the channel; an unobserved result is dropped once.
async i32 owned() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::channel<std.string::string> factory = std.sync::channel::<std.string::string>();
    std.sync::sender<std.string::string> sender = std.sync::sender(&factory);
    std.sync::receiver<std.string::string> inbox = std.sync::receiver(move factory);
    std.string::string text = std.string::from_str("hello");
    std.sync::send_result<std.string::string> result = std.sync::send(&sender, move text);
    i32 status = 0;
    switch (move result) {
    case variant std.sync::send_result::sent: break;
    case variant std.sync::send_result::disconnected(move value): status += 1; break;
    case variant std.sync::send_result::allocation_failed(move value): status += 1; break;
    }
    o<std.string::string> received = await inbox.receive();
    switch (move received) {
    case variant o::some(move value):
        str text = value;
        if (len(text) != 5usize) { status += 2; }
        break;
    case variant o::none: status += 4; break;
    }
    std.string::string again = std.string::from_str("dropped");
    std.sync::send_result<std.string::string> second = std.sync::send(&sender, move again);
    switch (move second) {
    case variant std.sync::send_result::sent: break;
    case variant std.sync::send_result::disconnected(move value): status += 8; break;
    case variant std.sync::send_result::allocation_failed(move value): status += 8; break;
    }
    task<o<std.string::string>> unobserved = inbox.receive();
    std.async::cancel(move unobserved);
    return status;
}

// L37.5: a switch on a place of a send result borrows it; the rejected value stays in it.
async i32 rejected() throws std.alloc::alloc_error {
    std.sync::channel<std.string::string> factory = std.sync::channel::<std.string::string>();
    std.sync::sender<std.string::string> sender = std.sync::sender(&factory);
    std.sync::receiver<std.string::string> inbox = std.sync::receiver(move factory);
    drop inbox;
    std.string::string text = std.string::from_str("lost");
    std.sync::send_result<std.string::string> result = std.sync::send(&sender, move text);
    i32 status = 0;
    switch (result) {
    case variant std.sync::send_result::sent: status += 1;
    case variant std.sync::send_result::disconnected(value):
        if (std.string::len(value) != 4usize) { status += 2; }
    case variant std.sync::send_result::allocation_failed(value): status += 4;
    }
    switch (move result) {
    case variant std.sync::send_result::sent: status += 8;
    case variant std.sync::send_result::disconnected(move value):
        if (std.string::len(&value) != 4usize) { status += 16; }
    case variant std.sync::send_result::allocation_failed(move value): status += 32;
    }
    return status;
}

// 5. Bounded and rendezvous channels.
async i32 bounded() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::sync_channel<i32> ring = std.sync::sync_channel::<i32>(2usize);
    std.sync::sync_sender<i32> ring_sender = std.sync::sync_sender(&ring);
    std.sync::receiver<i32> ring_inbox = std.sync::sync_receiver(move ring);
    task<o<i32>> waiting = ring_inbox.receive();
    bool first = sent(std.sync::sync_send(&ring_sender, 3));
    o<i32> three = await move waiting;
    bool second = sent(std.sync::sync_send(&ring_sender, 4));
    o<i32> four = await ring_inbox.receive();
    std.sync::sync_channel<i32> meeting = std.sync::sync_channel::<i32>(0usize);
    std.sync::sync_sender<i32> meeting_sender = std.sync::sync_sender(&meeting);
    std.sync::receiver<i32> meeting_inbox = std.sync::sync_receiver(move meeting);
    task<o<i32>> rendezvous = meeting_inbox.receive();
    bool third = sent(std.sync::sync_send(&meeting_sender, 5));
    o<i32> five = await move rendezvous;
    i32 status = 0;
    if (first == false) { status += 1; }
    if (second == false) { status += 1; }
    if (third == false) { status += 1; }
    if (value_or(three, 0) != 3) { status += 2; }
    if (value_or(four, 0) != 4) { status += 4; }
    if (value_or(five, 0) != 5) { status += 8; }
    return status;
}

// 6. A synchronous function starts a receive for its caller.
task<o<i32>> start(const std.sync::receiver<i32>* inbox) throws std.async::start_error {
    return inbox->receive();
}

async i32 started() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::channel<i32> factory = std.sync::channel::<i32>();
    std.sync::sender<i32> sender = std.sync::sender(&factory);
    std.sync::receiver<i32> inbox = std.sync::receiver(move factory);
    task<o<i32>> pending = start(&inbox);
    bool accepted = sent(std.sync::send(&sender, 9));
    o<i32> value = await move pending;
    i32 status = 0;
    if (accepted == false) { status += 1; }
    if (value_or(value, 0) != 9) { status += 2; }
    return status;
}

// The exit status counts the failed cases, so any failure is a nonzero status.
async i32 main() {
    i32 failures = 0;
    if (await drained() != 0) { failures += 1; }
    if (await selected() != 0) { failures += 1; }
    if (await ordered() != 0) { failures += 1; }
    if (await owned() != 0) { failures += 1; }
    if (await rejected() != 0) { failures += 1; }
    if (await bounded() != 0) { failures += 1; }
    if (await started() != 0) { failures += 1; }
    return failures;
}
