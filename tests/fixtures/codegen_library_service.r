module test.codegen.library_service;
import std.service;

/* R-SLIB-SERVICE-0001..0003 (L24, L25.3): std.service::serve accepts TCP connections, runs one
   handler task per connection in a bounded group, rejects connections beyond its capacity or
   leaves them to the listener until a handler task has ended, bounds each connection by its
   timeout, reports handler errors and stops on request, on the end of every stop sender or by
   cancellation of its task; serve_with gives each handler the shared state. */

struct Tally { atomic u32 served; };

// Echo one message back to the client.
async void echo(std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(64usize, 0u8);
    task_scope(1) io {
        usize count = await std.net::tcp_read_into(&connection.stream, buffer.as_slice_mut());
        const u8[] data = buffer.as_slice();
        await std.net::tcp_write_all_from(&connection.stream, data[0usize..count]);
    }
}

// Echo one message back after a pause, so that a second connection finds the capacity in use.
async void slow_echo(std.net::tcp_connection connection) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 200000000u32));
    array<u8> buffer = std.alloc::bytes(64usize, 0u8);
    task_scope(1) io {
        usize count = await std.net::tcp_read_into(&connection.stream, buffer.as_slice_mut());
        const u8[] data = buffer.as_slice();
        await std.net::tcp_write_all_from(&connection.stream, data[0usize..count]);
    }
}

// Occupy a slot for a while.
async void hold(std.net::tcp_connection connection) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_seconds(1i64));
}

// Wait for data that never comes: the connection timeout ends the read.
async void stall(std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(16usize, 0u8);
    task_scope(1) io {
        usize count = await std.net::tcp_read_into(&connection.stream, buffer.as_slice_mut());
        count as void;
    }
}

// Fail with a conversion error.
async void fail(std.net::tcp_connection connection) throws std.error::fault {
    u16 port = std.convert::parse_u16("port", 10u32);
    port as void;
}

// Sleep beyond every timeout: only cancellation ends it (timers ignore deadlines).
async void sleeper(std.net::tcp_connection connection) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_seconds(30i64));
}

// Count the connection in the shared state; the peer then reads the end of the stream.
async void tally(arc Tally state, std.net::tcp_connection connection) throws std.error::fault {
    const Tally* shared = &*state;
    core::atomic_fetch_add(&shared->served, 1u32, core::memory_order::relaxed) as void;
}

async std.net::tcp_listener open()
    throws std.net::address_error, std.net::net_error, std.async::start_error {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(options);
    return move listener;
}

std.time::duration milliseconds_of(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts((count / 1000u32) as i64, (count % 1000u32) * 1000000u32);
}

std.service::options settings(u32 capacity, u32 milliseconds) throws std.time::duration_error {
    return std.service::options {.capacity = capacity, .timeout = milliseconds_of(milliseconds),
                                 .overflow = std.service::overflow::reject};
}

std.service::options waiting_settings(u32 capacity, u32 milliseconds)
    throws std.time::duration_error {
    return std.service::options {.capacity = capacity, .timeout = milliseconds_of(milliseconds),
                                 .overflow = std.service::overflow::wait};
}

async void pause(u32 milliseconds)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    await std.time::sleep_for(milliseconds_of(milliseconds));
}

void request(const std.sync::sender<std.service::stop>* stopper, std.service::stop mode) {
    std.sync::send_result<std.service::stop> sent = std.sync::send(stopper, mode);
    drop sent;
}

// Three echo exchanges, then a drain request.
async i32 echo_clients(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    i32 status = 0;
    for (i32 index = 0; index < 3; index += 1) {
        std.net::tcp_stream client = await endpoint.connect();
        array<u8> message = std.alloc::bytes(5usize, 65u8);
        array<u8> reply = std.alloc::bytes(16usize, 0u8);
        task_scope(1) io {
            await std.net::tcp_write_all_from(&client, message.as_slice());
            await client.shutdown(std.net::shutdown_direction::write);
            usize count = await std.net::tcp_read_into(&client, reply.as_slice_mut());
            if (count != 5usize) { status += 1; }
        }
    }
    request(&stopper, std.service::stop::drain);
    return status;
}

// Two connections at once against a capacity of one: the second is closed without a reply.
async i32 pair_clients(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error, std.time::time_error,
        std.time::duration_error {
    std.net::tcp_stream first = await endpoint.connect();
    await pause(50u32);
    std.net::tcp_stream second = await endpoint.connect();
    i32 status = 0;
    array<u8> reply = std.alloc::bytes(16usize, 0u8);
    try {
        task_scope(1) io {
            usize count = await std.net::tcp_read_into(&second, reply.as_slice_mut());
            if (count != 0usize) { status += 1; }
        }
    } catch (std.net::net_error failure) {
        failure as void;
    }
    request(&stopper, std.service::stop::drain);
    drop first;
    return status;
}

// Two connections at once against a capacity of one that waits: both are answered in turn.
async i32 queued_clients(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    std.net::tcp_stream first = await endpoint.connect();
    std.net::tcp_stream second = await endpoint.connect();
    array<u8> message = std.alloc::bytes(3usize, 66u8);
    array<u8> reply = std.alloc::bytes(16usize, 0u8);
    i32 status = 0;
    task_scope(1) io {
        await std.net::tcp_write_all_from(&first, message.as_slice());
        await first.shutdown(std.net::shutdown_direction::write);
        await std.net::tcp_write_all_from(&second, message.as_slice());
        await second.shutdown(std.net::shutdown_direction::write);
        usize one = await std.net::tcp_read_into(&first, reply.as_slice_mut());
        if (one != 3usize) { status += 1; }
        usize two = await std.net::tcp_read_into(&second, reply.as_slice_mut());
        if (two != 3usize) { status += 2; }
    }
    request(&stopper, std.service::stop::drain);
    return status;
}

// One connection kept open for a while, then a stop request of the given mode.
async i32 idle_client(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper,
                      u32 milliseconds, std.service::stop mode) throws std.net::net_error, std.async::start_error, std.time::time_error, std.time::duration_error {
    std.net::tcp_stream client = await endpoint.connect();
    await pause(milliseconds);
    request(&stopper, mode);
    drop client;
    return 0;
}

// Connections that wait until their handler has ended, then a drain request.
async i32 counted_clients(std.net::socket_address endpoint,
                          std.sync::sender<std.service::stop> stopper) throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    i32 status = 0;
    for (i32 index = 0; index < 3; index += 1) {
        std.net::tcp_stream client = await endpoint.connect();
        array<u8> reply = std.alloc::bytes(4usize, 0u8);
        task_scope(1) io {
            usize count = await std.net::tcp_read_into(&client, reply.as_slice_mut());
            if (count != 0usize) { status += 1; }
        }
    }
    request(&stopper, std.service::stop::drain);
    return status;
}

std.sync::channel<std.service::stop> stop_channel() throws std.alloc::alloc_error {
    return std.sync::channel::<std.service::stop>();
}

// 1. Echo exchanges complete and the drain returns the account.
async i32 echoes()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(4u32, 5000u32), move stop, echo);
        auto client = echo_clients(endpoint, move stopper);
        status += await move client;
        std.service::report account = await move server;
        if (account.accepted != 3u64 || account.completed != 3u64) { status += 2; }
        if (account.rejected != 0u64 || account.failed != 0u64 || account.cancelled != 0u64) {
            status += 4;
        }
    }
    return status;
}

// 2. A connection beyond the capacity is rejected.
async i32 overflow()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(1u32, 5000u32), move stop, hold);
        auto client = pair_clients(endpoint, move stopper);
        status += await move client;
        std.service::report account = await move server;
        if (account.accepted != 1u64 || account.rejected != 1u64) { status += 2; }
        if (account.completed != 1u64) { status += 4; }
    }
    return status;
}

// 3. The connection timeout ends a stalled handler, whose error the account keeps.
async i32 stalled()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(2u32, 100u32), move stop, stall);
        auto client = idle_client(endpoint, move stopper, 400u32, std.service::stop::drain);
        status += await move client;
        std.service::report account = await move server;
        if (account.failed != 1u64) { status += 2; }
        switch (account.last_failure) {
        case variant o::some(error):
            if (error->domain != std.error::domain::network) { status += 4; }
            switch (std.error::name(*error)) {
            case "timed_out": break;
            default: status += 8;
            }
        case variant o::none: status += 16;
        }
    }
    return status;
}

// 4. A handler error is counted and kept.
async i32 failing()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(2u32, 5000u32), move stop, fail);
        auto client = idle_client(endpoint, move stopper, 300u32, std.service::stop::drain);
        status += await move client;
        std.service::report account = await move server;
        if (account.failed != 1u64 || account.completed != 0u64) { status += 2; }
        switch (account.last_failure) {
        case variant o::some(error):
            if (error->domain != std.error::domain::conversion) { status += 4; }
        case variant o::none: status += 8;
        }
    }
    return status;
}

// 5. A cancel request cancels a running handler at once.
async i32 cancelling()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(2u32, 5000u32), move stop, sleeper);
        auto client = idle_client(endpoint, move stopper, 300u32, std.service::stop::cancel);
        status += await move client;
        std.service::report account = await move server;
        if (account.accepted != 1u64 || account.cancelled != 1u64) { status += 2; }
    }
    return status;
}

// 6. A drain waits at most the timeout, then cancels the handlers still running.
async i32 draining()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(2u32, 200u32), move stop, sleeper);
        auto client = idle_client(endpoint, move stopper, 300u32, std.service::stop::drain);
        status += await move client;
        std.service::report account = await move server;
        if (account.accepted != 1u64 || account.cancelled != 1u64) { status += 2; }
    }
    return status;
}

// 7. The end of every stop sender stops the service like a drain request.
async i32 disconnecting()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.service::report account = await std.service::serve(move listener, settings(2u32, 100u32), move stop, echo);
    i32 status = 0;
    if (account.accepted != 0u64) { status += 1; }
    return status;
}

// 8. A capacity beyond the largest one is refused before any connection is accepted.
async std.service::report serve_oversized()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    return await std.service::serve(move listener, settings(2048u32, 100u32), move stop, echo);
}

async i32 oversized()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    i32 status = 1;
    try {
        std.service::report account = await serve_oversized();
        account as void;
    } catch (std.async::start_error failure) {
        if (failure == std.async::start_error::scope_full) { status = 0; }
    }
    return status;
}

// 9. Cancelling the serving task cancels its handlers and closes the listener.
async i32 aborting()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.net::tcp_stream client = await endpoint.connect();
    task_scope(2) group {
        auto server = std.service::serve(move listener, settings(2u32, 5000u32), move stop, sleeper);
        await pause(100u32);
        std.async::cancel(move server);
        await group.all();
    }
    drop client;
    i32 status = 0;
    try {
        std.net::tcp_stream again = await endpoint.connect();
        drop again;
        status += 1;
    } catch (std.net::net_error failure) {
        failure as void;
    }
    request(&stopper, std.service::stop::drain);
    return status;
}

// 10. serve_with gives every handler a clone of the shared state.
async i32 sharing()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Tally state = new arc Tally {.served = 0u32};
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve_with(move listener, settings(2u32, 5000u32), move stop,
                                              std.arc::clone(&state), tally);
        auto client = counted_clients(endpoint, move stopper);
        status += await move client;
        std.service::report account = await move server;
        if (account.completed != 3u64) { status += 2; }
    }
    const Tally* counts = &*state;
    if (core::atomic_load(&counts->served, core::memory_order::relaxed) != 3u32) { status += 4; }
    return status;
}

// 11. At its capacity a waiting service accepts nothing until a handler task has ended; the next
// connection stays with the listener and is served then.
async i32 queueing()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    task_scope(2) group {
        auto server = std.service::serve(move listener, waiting_settings(1u32, 5000u32), move stop,
                                         slow_echo);
        auto client = queued_clients(endpoint, move stopper);
        status += await move client;
        std.service::report account = await move server;
        if (account.accepted != 2u64 || account.rejected != 0u64) { status += 4; }
        if (account.completed != 2u64) { status += 8; }
    }
    return status;
}

// 12. A waiting service needs a capacity of at least one.
async std.service::report serve_without_room()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    std.net::tcp_listener listener = await open();
    std.sync::channel<std.service::stop> factory = stop_channel();
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    return await std.service::serve(move listener, waiting_settings(0u32, 100u32), move stop, echo);
}

async i32 without_room()
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error, std.time::duration_error {
    i32 status = 1;
    try {
        std.service::report account = await serve_without_room();
        account as void;
    } catch (std.async::start_error failure) {
        if (failure == std.async::start_error::scope_full) { status = 0; }
    }
    return status;
}

// The exit status counts the failed cases, so any failure is a nonzero status.
async i32 main() {
    i32 failures = 0;
    if (await echoes() != 0) { failures += 1; }
    if (await overflow() != 0) { failures += 1; }
    if (await stalled() != 0) { failures += 1; }
    if (await failing() != 0) { failures += 1; }
    if (await cancelling() != 0) { failures += 1; }
    if (await draining() != 0) { failures += 1; }
    if (await disconnecting() != 0) { failures += 1; }
    if (await oversized() != 0) { failures += 1; }
    if (await aborting() != 0) { failures += 1; }
    if (await sharing() != 0) { failures += 1; }
    if (await queueing() != 0) { failures += 1; }
    if (await without_room() != 0) { failures += 1; }
    return failures;
}
