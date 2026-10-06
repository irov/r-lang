module example.service.handlers;
import std.alloc;
import std.log;
import example.service.wire;

/* The running total that every count connection adds to, under an asynchronous lock, the
   bounded channel that reports each amount to the auditor, and a logger of the service. */
struct Total {
    std.async::mutex<u64> value;
    std.sync::sync_sender<u64> audit;
    std.log::logger journal;
};

// Echo the request back.
async void echo(std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(64usize, 0u8);
    task_scope(1) io {
        usize length = await example.service.wire::read_all(&connection.stream, buffer.as_slice_mut());
        const u8[] request = buffer.as_slice();
        await std.net::tcp_write_all_from(&connection.stream, request[0usize..length]);
    }
}

// The decimal amount of a request.
u64 amount_of(const u8[] request) throws std.error::fault {
    std.string::string text = std.string::from_utf8(request);
    return std.convert::parse_u64(text, 10u32);
}

/* Add the amount to the total and report it while the guard is still held, so the auditor sees
   the amounts in the order they entered the total. try_reserve takes a free slot of the bounded
   channel at once; when the auditor is behind, reserve waits for room instead of growing a
   queue. A permit sends into the slot it holds and never waits. */
async u64 add(arc Total total, u64 amount) throws std.error::fault {
    std.async::mutex_guard<u64> guard = await total->value.lock();
    *(guard.get_mut()) += amount;
    u64 sum = *(guard.get());
    /* The fields are built only when a debug record would be written. */
    if (total->journal.enabled(std.log::level::debug) == true) {
        std.log::fields extra = std.log::fields::create();
        extra.number("amount", std.convert::checked_i64(amount));
        extra.number("total", std.convert::checked_i64(sum));
        total->journal.log(std.log::level::debug, "amount added", &extra);
    }
    std.sync::try_reserve_result<u64> slot = total->audit.try_reserve();
    switch (move slot) {
    case variant std.sync::try_reserve_result::reserved(move permit): (move permit).send(amount);
    case variant std.sync::try_reserve_result::full:
        std.sync::reserve_result<u64> room = await total->audit.reserve();
        switch (move room) {
        case variant std.sync::reserve_result::reserved(move permit): (move permit).send(amount);
        case variant std.sync::reserve_result::disconnected: break;
        }
    case variant std.sync::try_reserve_result::disconnected: break;
    }
    (move guard).unlock();
    return sum;
}

// Add the request to the shared total and reply with the new total.
async void count(arc Total total, std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(32usize, 0u8);
    usize length = 0usize;
    task_scope(1) io {
        length += await example.service.wire::read_all(&connection.stream, buffer.as_slice_mut());
    }
    const u8[] request = buffer.as_slice();
    u64 amount = amount_of(request[0usize..length]);
    u64 sum = await add(move total, amount);
    std.string::string reply = f"{sum}";
    task_scope(1) io {
        await std.net::tcp_write_all_from(&connection.stream, reply.as_str());
    }
}

/* The most bytes that a repeat handler may hold while it builds its reply. */
struct Repeat { usize budget_bytes; };

/* The lines of a reply of size bytes, at most 64 bytes each. An allocation that the budget of
   the handler refuses ends the build with budget_exhausted, and the lines built so far are
   released as the error leaves this function. */
array<array<u8>> lines_of(u64 size) throws std.alloc::alloc_error {
    array<array<u8>> lines = std.array::create::<array<u8>>();
    u64 left = size;
    while (left > 0u64) {
        usize width = 64usize;
        if (left < 64u64) { width = left as usize; }
        array<u8> line = std.alloc::bytes(width, 120u8);
        try {
            lines.push(move line);
        } catch (std.array::push_error<array<u8>> failure) {
            switch (move failure) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
        left -= width as u64;
    }
    return move lines;
}

/* Build a reply of the requested size inside a budget of budget_bytes (Core R-STMT-0020) and
   answer with what was built, or with the refusal of the budget. The connection, its buffer and
   the reply live outside the budget; only the build is charged to it. */
async void repeat(arc Repeat settings, std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(32usize, 0u8);
    usize length = 0usize;
    task_scope(1) io {
        length += await example.service.wire::read_all(&connection.stream, buffer.as_slice_mut());
    }
    const u8[] request = buffer.as_slice();
    u64 size = amount_of(request[0usize..length]);
    o<usize> built = o::none;
    o<std.alloc::alloc_error> refused = o::none;
    budget (std.alloc::limits {.bytes = o::some(settings->budget_bytes)}) {
        try {
            array<array<u8>> lines = lines_of(size);
            core::replace(&built, o::some(len(lines))) as void;
            drop lines;
        } catch (std.alloc::alloc_error failure) {
            refused = o::some(failure);
        }
    }
    std.string::string reply = std.string::create();
    switch (built) {
    case variant o::some(total):
        usize pieces = *total;
        std.string::string text = f"{size} bytes in {pieces} lines";
        reply.append(text);
    case variant o::none: break;
    }
    switch (refused) {
    case variant o::some(failure):
        str name = core::enum_name(*failure);
        std.string::string text = f"refused {name}";
        reply.append(text);
    case variant o::none: break;
    }
    task_scope(1) io {
        await std.net::tcp_write_all_from(&connection.stream, reply.as_str());
    }
}
