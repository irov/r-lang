module example.service.books;
import example.service.wire;
import example.service.handlers;

/* The largest amount that the watch mode accepts; the request `limit=N` replaces it. */
struct Limits { u64 most; };

/* The state that every watch connection shares: the total under a lock, the limits under a
   read-write lock that handlers read at once, a gate whose permits main hands out once the
   watcher listens, and the broadcast that publishes every new total. */
struct Books {
    std.async::mutex<u64> total;
    std.async::rw_lock<Limits> limits;
    std.async::semaphore gate;
    std.async::broadcast<u64> totals;
};

enum Command { amount, limit, status };

Command command_of(const u8[] request) {
    if (std.bytes::equal(request, "status") == true) { return Command::status; }
    if (std.bytes::starts_with(request, "limit=") == true) { return Command::limit; }
    return Command::amount;
}

/* Add an amount that the limits accept under the lock of the total, and publish the new total
   before the guard is released, so the totals reach the watcher in order. Each guard and the
   permit belong to this task and stay valid across every await. */
async std.string::string credit(arc Books shared, std.async::semaphore_permit permit, u64 amount)
    throws std.error::fault {
    std.async::rw_read_guard<Limits> limits = await shared->limits.read();
    u64 most = (limits.get())->most;
    (move limits).unlock();
    if (amount > most) {
        (move permit).release();
        return f"refused above {most}";
    }
    std.async::mutex_guard<u64> total = await shared->total.lock();
    *(total.get_mut()) += amount;
    u64 sum = *(total.get());
    shared->totals.send(sum) as void;
    (move total).unlock();
    (move permit).release();
    return f"{sum}";
}

/* Take a permit of the gate, a free one at once or else the next one that main or a handler
   gives back, then add the amount. */
async std.string::string deposit(arc Books shared, u64 amount) throws std.error::fault {
    o<std.async::semaphore_permit> free = shared->gate.try_acquire();
    switch (move free) {
    case variant o::some(move permit): return await credit(move shared, move permit, amount);
    case variant o::none: break;
    }
    std.async::semaphore_permit permit = await shared->gate.acquire();
    return await credit(move shared, move permit, amount);
}

std.string::string replace(std.async::rw_write_guard<Limits> guard, u64 most)
    throws std.error::fault {
    u64 before = (guard.get())->most;
    (guard.get_mut())->most = most;
    (move guard).unlock();
    return f"was {before}";
}

/* Replace the limit under the write lock: a free lock is taken at once, a busy one when its
   readers are done. */
async std.string::string set_limit(arc Books shared, u64 most) throws std.error::fault {
    o<std.async::rw_write_guard<Limits>> free = shared->limits.try_write();
    switch (move free) {
    case variant o::some(move guard): return replace(move guard, most);
    case variant o::none: break;
    }
    std.async::rw_write_guard<Limits> guard = await shared->limits.write();
    return replace(move guard, most);
}

// The total, or none while a handler holds its lock.
o<u64> total_now(const Books* shared) {
    o<std.async::mutex_guard<u64>> held = shared->total.try_lock();
    switch (move held) {
    case variant o::some(move guard):
        u64 value = *(guard.get());
        (move guard).unlock();
        return o::some(value);
    case variant o::none: return o::none;
    }
}

// The limit, or none while `limit=N` holds the write lock.
o<u64> limit_now(const Books* shared) {
    o<std.async::rw_read_guard<Limits>> held = shared->limits.try_read();
    switch (move held) {
    case variant o::some(move guard):
        u64 value = (guard.get())->most;
        (move guard).unlock();
        return o::some(value);
    case variant o::none: return o::none;
    }
}

std.string::string shown(o<u64> value) throws std.error::fault {
    switch (value) {
    case variant o::some(number):
        u64 known = *number;
        return f"{known}";
    case variant o::none: return std.string::from_str("busy");
    }
}

// Report the state without waiting for any lock.
std.string::string status_of(const Books* shared) throws std.error::fault {
    std.string::string total = shown(total_now(shared));
    std.string::string limit = shown(limit_now(shared));
    usize free = shared->gate.available_permits();
    return f"total={total} limit={limit} free={free}";
}

// The number that a request carries: the amount, the new limit, or none for status.
u64 value_of(Command command, const u8[] request) throws std.error::fault {
    switch (command) {
    case Command::amount: return example.service.handlers::amount_of(request);
    case Command::limit: return example.service.handlers::amount_of(request[6usize..len(request)]);
    case Command::status: return 0u64;
    }
}

async std.string::string answer(arc Books shared, Command command, u64 value)
    throws std.error::fault {
    switch (command) {
    case Command::amount: return await deposit(move shared, value);
    case Command::limit: return await set_limit(move shared, value);
    case Command::status: return status_of(&*shared);
    }
}

// Answer one request of the watch mode: an amount, `limit=N` or `status`.
async void handle(arc Books shared, std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(32usize, 0u8);
    usize length = 0usize;
    task_scope(1) io {
        length += await example.service.wire::read_all(&connection.stream, buffer.as_slice_mut());
    }
    const u8[] bytes = buffer.as_slice();
    const u8[] request = bytes[0usize..length];
    Command command = command_of(request);
    u64 value = value_of(command, request);
    std.string::string reply = await answer(move shared, command, value);
    task_scope(1) io {
        await std.net::tcp_write_all_from(&connection.stream, reply);
    }
}

/* Listen to the totals and tell main so, then wait until the service has ended and read what the
   queue kept: the oldest totals are gone, which the first receive reports as lagged, and closed
   follows once no broadcast handle is left. The wait for `closing` starts before main hears that
   the watcher listens, so the notify_all of main cannot miss it. */
async std.string::string watch_totals(std.async::broadcast<u64> totals, std.async::notify ready,
                                      std.async::notify closing) throws std.error::fault {
    std.async::broadcast_receiver<u64> inbox = totals.subscribe();
    drop totals;
    task<void> finished = closing.notified();
    ready.notify_one();
    await move finished;
    std.string::string line = std.string::from_str("watch");
    bool open = true;
    while (open == true) {
        std.async::broadcast_result<u64> next = await inbox.receive();
        switch (move next) {
        case variant std.async::broadcast_result::received(move total):
            std.string::string piece = f" {total}";
            line.append(piece);
        case variant std.async::broadcast_result::lagged(move missed):
            std.string::string piece = f" lagged={missed}";
            line.append(piece);
        case variant std.async::broadcast_result::closed: open = false;
        }
    }
    drop inbox;
    line.append("\n");
    return move line;
}
