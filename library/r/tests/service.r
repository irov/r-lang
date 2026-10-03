module tests.std.service;
import std.test;
import std.fs;
import std.service;

// The tests of std.service (Library R-SLIB-SERVICE-0001..0003): serve and serve_with run one
// handler task per loopback connection in a bounded group, reject or hold connections beyond the
// capacity, bound each connection by its timeout, count and keep handler failures, and stop on a
// drain or cancel request or when every stop sender is gone. Handlers announce themselves with a
// byte and clients release them by ending their write direction, so no outcome depends on a
// race of timers. Run in test mode (Core R-FUNC-0025).

/* Every byte that the stream reads until its end. */
@scoped
protected async usize read_all(const std.net::tcp_stream* stream, bytes* out)
    throws std.error::fault {
    u8[16] chunk = {};
    usize total = 0usize;
    while (true) {
        usize count = 0usize;
        task_scope(1) io { count += await std.net::tcp_read_into(stream, &chunk); }
        if (count == 0usize) { break; }
        out->append(chunk[0usize..count]);
        total += count;
    }
    return total;
}

/* Writes "!" to the client, then echoes every byte it sends until the end of its bytes. */
protected async void announce_and_echo(std.net::tcp_connection connection)
    throws std.error::fault {
    u8[1] ready = {33u8};
    bytes received = {};
    usize count = 0usize;
    task_scope(1) hello { await std.net::tcp_write_all_from(&connection.stream, ready[0usize..1usize]); }
    task_scope(1) input { count += await read_all(&connection.stream, &received); }
    task_scope(1) output { await std.net::tcp_write_all_from(&connection.stream, received.as_slice()); }
}

/* Writes "!" to the client, then sleeps beyond every timeout: only cancellation ends it. */
protected async void announce_and_sleep(std.net::tcp_connection connection)
    throws std.error::fault {
    u8[1] ready = {33u8};
    task_scope(1) hello { await std.net::tcp_write_all_from(&connection.stream, ready[0usize..1usize]); }
    await std.time::sleep_for(std.time::duration_from_seconds(30i64));
}

/* Fails with a conversion error. */
protected async void fail_to_parse(std.net::tcp_connection connection) throws std.error::fault {
    u16 port = std.convert::parse_u16("port", 10u32);
    port as void;
}

/* Waits for bytes that never come: the connection timeout ends the read. */
protected async void stall(std.net::tcp_connection connection) throws std.error::fault {
    bytes buffer = std.alloc::bytes(16usize, 0u8);
    task_scope(1) io {
        usize count = await std.net::tcp_read_into(&connection.stream, buffer.as_slice_mut());
        count as void;
    }
}

struct Tally { atomic u32 served; };

/* Counts the connection in the shared state. */
protected async void count_visit(arc Tally state, std.net::tcp_connection connection)
    throws std.error::fault {
    const Tally* shared = &*state;
    core::atomic_fetch_add(&shared->served, 1u32, core::memory_order::relaxed) as void;
}

protected async std.net::tcp_listener open_listener() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(options);
    return move listener;
}

protected std.service::options options_of(u32 capacity, u32 milliseconds,
                                          std.service::overflow policy)
    throws std.time::duration_error {
    std.time::duration timeout = std.time::duration_from_parts(
        (milliseconds / 1000u32) as i64, (milliseconds % 1000u32) * 1000000u32);
    return std.service::options {.capacity = capacity, .timeout = timeout, .overflow = policy};
}

protected void request(const std.sync::sender<std.service::stop>* stopper, std.service::stop mode) {
    std.sync::send_result<std.service::stop> sent = std.sync::send(stopper, mode);
    drop sent;
}

/* Sends text on a new connection, ends its write direction and returns the whole reply. */
@scoped
protected async std.string::string exchange(std.net::socket_address endpoint, str text)
    throws std.error::fault {
    std.string::string message = std.string::from_str(text);
    std.net::tcp_stream client = await endpoint.connect();
    bytes reply = {};
    usize count = 0usize;
    task_scope(1) io {
        await std.net::tcp_write_all_from(&client, message.as_bytes());
        await std.net::tcp_shutdown(&client, std.net::shutdown_direction::write);
        count += await read_all(&client, &reply);
    }
    return std.string::from_utf8(reply.as_slice());
}

/* Connects and waits until the handler closes the connection. */
protected async usize visit(std.net::socket_address endpoint) throws std.error::fault {
    std.net::tcp_stream client = await endpoint.connect();
    bytes reply = {};
    usize count = 0usize;
    task_scope(1) io { count += await read_all(&client, &reply); }
    return count;
}

/* Three echo exchanges, one after another, then a drain request. */
protected async u32 echo_clients(std.net::socket_address endpoint,
                                 std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    u32 answered = 0u32;
    for (u32 index = 0u32; index < 3u32; index += 1u32) {
        task_scope(1) talk {
            std.string::string reply = await exchange(endpoint, "hello");
            if (std.bytes::equal(reply.as_bytes(), "!hello") == true) { answered += 1u32; }
        }
    }
    request(&stopper, std.service::stop::drain);
    return answered;
}

@test
async void serves_connections_until_a_drain() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    task_scope(2) group {
        auto server = std.service::serve(move listener,
                                         options_of(4u32, 5000u32, std.service::overflow::wait),
                                         move stop, announce_and_echo);
        auto client = echo_clients(endpoint, move stopper);
        u32 answered = await move client;
        std.service::report account = await move server;
        std.test::equal(answered, 3u32);
        std.test::equal(account.accepted, 3u64);
        std.test::equal(account.completed, 3u64);
        std.test::equal(account.rejected, 0u64);
        std.test::equal(account.failed, 0u64);
        std.test::equal(account.cancelled, 0u64);
        switch (account.last_failure) {
        case variant o::some(error): std.test::fail("no handler failed");
        case variant o::none: break;
        }
    }
}

/* The first connection holds the only slot while a second one arrives, which the service closes;
   then the first ends its bytes and the service is drained. Returns the bytes of the second. */
protected async usize crowding_clients(std.net::socket_address endpoint,
                                       std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    std.net::tcp_stream first = await endpoint.connect();
    bytes hello = std.alloc::bytes(1usize, 0u8);
    usize announced = 0usize;
    task_scope(1) io { announced += await std.net::tcp_read_into(&first, hello.as_slice_mut()); }
    std.net::tcp_stream second = await endpoint.connect();
    bytes refused = {};
    usize closed = 0usize;
    try {
        task_scope(1) io { closed += await read_all(&second, &refused); }
    } catch (std.net::net_error failure) {
        failure as void;
    }
    bytes rest = {};
    task_scope(1) io {
        await std.net::tcp_shutdown(&first, std.net::shutdown_direction::write);
        usize count = await read_all(&first, &rest);
        count as void;
    }
    request(&stopper, std.service::stop::drain);
    return closed + (1usize - announced);
}

@test
async void rejects_connections_beyond_the_capacity() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    task_scope(2) group {
        auto server = std.service::serve(move listener,
                                         options_of(1u32, 5000u32, std.service::overflow::reject),
                                         move stop, announce_and_echo);
        auto client = crowding_clients(endpoint, move stopper);
        usize unexpected = await move client;
        std.service::report account = await move server;
        std.test::equal(unexpected, 0usize);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.rejected, 1u64);
        std.test::equal(account.completed, 1u64);
    }
    /* A capacity of zero that rejects closes every connection. */
    std.net::tcp_listener closed_door = await open_listener();
    std.net::socket_address door = closed_door.local_address();
    std.sync::channel<std.service::stop> second_factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> second_stopper = std.sync::sender(&second_factory);
    std.sync::receiver<std.service::stop> second_stop = std.sync::receiver(move second_factory);
    task_scope(2) group {
        auto server = std.service::serve(move closed_door,
                                         options_of(0u32, 5000u32, std.service::overflow::reject),
                                         move second_stop, announce_and_echo);
        usize read = 0usize;
        try {
            read += await visit(door);
        } catch (std.net::net_error failure) {
            failure as void;
        }
        request(&second_stopper, std.service::stop::drain);
        std.service::report account = await move server;
        std.test::equal(read, 0usize);
        std.test::equal(account.accepted, 0u64);
        std.test::equal(account.rejected, 1u64);
    }
}

/* The first connection holds the only slot while a second one waits with the listener; when the
   first ends, the second is served. Returns the announcement of the first and the reply of the
   second. */
protected async std.string::string queued_clients(std.net::socket_address endpoint,
                                                  std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    std.net::tcp_stream first = await endpoint.connect();
    bytes hello = std.alloc::bytes(1usize, 0u8);
    usize announced = 0usize;
    task_scope(1) io { announced += await std.net::tcp_read_into(&first, hello.as_slice_mut()); }
    std.net::tcp_stream second = await endpoint.connect();
    std.string::string message = std.string::from_str("second");
    bytes rest = {};
    bytes reply = {};
    task_scope(1) io {
        await std.net::tcp_write_all_from(&second, message.as_bytes());
        await std.net::tcp_shutdown(&second, std.net::shutdown_direction::write);
        await std.net::tcp_shutdown(&first, std.net::shutdown_direction::write);
        usize count = await read_all(&first, &rest);
        count as void;
        usize answer = await read_all(&second, &reply);
        answer as void;
    }
    request(&stopper, std.service::stop::drain);
    std.string::string result = std.string::from_utf8(hello[0usize..announced]);
    std.string::append_utf8(&result, reply.as_slice());
    return move result;
}

@test
async void holds_connections_until_a_slot_is_free() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    task_scope(2) group {
        auto server = std.service::serve(move listener,
                                         options_of(1u32, 5000u32, std.service::overflow::wait),
                                         move stop, announce_and_echo);
        auto client = queued_clients(endpoint, move stopper);
        std.string::string reply = await move client;
        std.service::report account = await move server;
        std.test::equal_text(reply.as_str(), "!!second");
        std.test::equal(account.accepted, 2u64);
        std.test::equal(account.rejected, 0u64);
        std.test::equal(account.completed, 2u64);
    }
}

/* One connection that waits until its handler has ended, then a drain request. */
protected async usize single_visit(std.net::socket_address endpoint,
                                   std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    usize count = await visit(endpoint);
    request(&stopper, std.service::stop::drain);
    return count;
}

@test
async void keeps_the_last_handler_failure() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    task_scope(2) group {
        auto server = std.service::serve(move listener,
                                         options_of(2u32, 5000u32, std.service::overflow::wait),
                                         move stop, fail_to_parse);
        auto client = single_visit(endpoint, move stopper);
        usize read = await move client;
        std.service::report account = await move server;
        std.test::equal(read, 0usize);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.failed, 1u64);
        std.test::equal(account.completed, 0u64);
        switch (account.last_failure) {
        case variant o::some(error):
            std.test::check(error->domain == std.error::domain::conversion, "a conversion error");
        case variant o::none: std.test::fail("the failure is kept");
        }
    }
    /* The timeout of a connection ends a handler that waits for bytes that never come. */
    std.net::tcp_listener slow = await open_listener();
    std.net::socket_address slow_end = slow.local_address();
    std.sync::channel<std.service::stop> slow_factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> slow_stopper = std.sync::sender(&slow_factory);
    std.sync::receiver<std.service::stop> slow_stop = std.sync::receiver(move slow_factory);
    task_scope(2) group {
        auto server = std.service::serve(move slow,
                                         options_of(2u32, 100u32, std.service::overflow::wait),
                                         move slow_stop, stall);
        auto client = single_visit(slow_end, move slow_stopper);
        usize read = await move client;
        std.service::report account = await move server;
        std.test::equal(read, 0usize);
        std.test::equal(account.failed, 1u64);
        switch (account.last_failure) {
        case variant o::some(error):
            std.test::check(error->domain == std.error::domain::network, "a network error");
            auto name = std.error::name(*error);
            std.test::equal_text(name, "timed_out");
        case variant o::none: std.test::fail("the timeout is kept");
        }
    }
}

/* Connects, waits until the handler runs, then sends a stop request of the given mode. */
protected async usize stopping_client(std.net::socket_address endpoint,
                                      std.sync::sender<std.service::stop> stopper,
                                      std.service::stop mode) throws std.error::fault {
    std.net::tcp_stream client = await endpoint.connect();
    bytes hello = std.alloc::bytes(1usize, 0u8);
    usize count = 0usize;
    task_scope(1) io { count += await std.net::tcp_read_into(&client, hello.as_slice_mut()); }
    request(&stopper, mode);
    drop client;
    return count;
}

@test
async void stop_requests_end_running_handlers() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    task_scope(2) group {
        auto server = std.service::serve(move listener,
                                         options_of(2u32, 5000u32, std.service::overflow::wait),
                                         move stop, announce_and_sleep);
        auto client = stopping_client(endpoint, move stopper, std.service::stop::cancel);
        usize announced = await move client;
        std.service::report account = await move server;
        std.test::equal(announced, 1usize);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.cancelled, 1u64);
        std.test::equal(account.completed, 0u64);
    }
    /* A drain waits at most the timeout for the running handlers, then cancels them. */
    std.net::tcp_listener second = await open_listener();
    std.net::socket_address second_end = second.local_address();
    std.sync::channel<std.service::stop> second_factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> second_stopper = std.sync::sender(&second_factory);
    std.sync::receiver<std.service::stop> second_stop = std.sync::receiver(move second_factory);
    task_scope(2) group {
        auto server = std.service::serve(move second,
                                         options_of(2u32, 100u32, std.service::overflow::wait),
                                         move second_stop, announce_and_sleep);
        auto client = stopping_client(second_end, move second_stopper, std.service::stop::drain);
        usize announced = await move client;
        std.service::report account = await move server;
        std.test::equal(announced, 1usize);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.cancelled, 1u64);
    }
}

/* Three connections that each wait until their handler has ended, then a drain request. */
protected async usize counted_visits(std.net::socket_address endpoint,
                                     std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    usize total = 0usize;
    for (u32 index = 0u32; index < 3u32; index += 1u32) { total += await visit(endpoint); }
    request(&stopper, std.service::stop::drain);
    return total;
}

@test
async void serve_with_shares_its_state() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Tally state = new arc Tally {.served = 0u32};
    task_scope(2) group {
        auto server = std.service::serve_with(move listener,
                                              options_of(2u32, 5000u32, std.service::overflow::wait),
                                              move stop, std.arc::clone(&state), count_visit);
        auto client = counted_visits(endpoint, move stopper);
        usize read = await move client;
        std.service::report account = await move server;
        std.test::equal(read, 0usize);
        std.test::equal(account.accepted, 3u64);
        std.test::equal(account.completed, 3u64);
    }
    const Tally* counts = &*state;
    std.test::equal(core::atomic_load(&counts->served, core::memory_order::relaxed), 3u32);
}

struct Stopper { std.sync::sender<std.service::stop> sender; };

/* Sends a drain request and returns, so the request and the outcome of the handler reach the
   loop at almost the same moment, while the loop waits at its capacity. */
protected async void stop_and_return(arc Stopper state, std.net::tcp_connection connection)
    throws std.error::fault {
    request(&state->sender, std.service::stop::drain);
    drop connection;
}

/* One service whose only handler requests the drain itself; the sender stays in the state, so
   a lost request would leave the service running past the limit, and a lost outcome would be
   counted as cancelled after the timeout. True when the service stopped in time with the
   handler counted as completed. */
protected async bool stops_with_its_handler() throws std.error::fault {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Stopper state = new arc Stopper {.sender = move stopper};
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit = std.time::instant_add(now, std.time::duration_from_seconds(5i64));
    bool exact = false;
    task_scope(2) group {
        auto server = std.service::serve_with(move listener,
                                              options_of(1u32, 30000u32, std.service::overflow::wait),
                                              move stop, move state, stop_and_return);
        auto client = visit(endpoint);
        select (group) {
        case std.service::report account = await move server:
            exact = account.accepted == 1u64 && account.completed == 1u64 && account.cancelled == 0u64;
        case until (limit): break;
        }
        group.cancel_all();
    }
    return exact;
}

/* P4.1-5: a stop request that arrives with the outcome of a handler loses neither. */
@test
async void counts_a_handler_that_requests_the_stop() throws std.error::fault, std.test::failure {
    for (u32 index = 0u32; index < 100u32; index += 1u32) {
        bool exact = await stops_with_its_handler();
        std.test::check(exact == true, "the request and the outcome both count");
    }
}

@test
async void stops_when_every_sender_is_gone() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.service::report account = await std.service::serve(
        move listener, options_of(2u32, 100u32, std.service::overflow::wait), move stop,
        announce_and_echo);
    std.test::equal(account.accepted, 0u64);
    std.test::equal(account.rejected, 0u64);
    std.test::equal(account.cancelled, 0u64);
}

/* Whether serve refuses the settings with scope_full before it accepts a connection. */
protected async bool refuses(std.service::options chosen) throws std.error::fault,
    std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    try {
        std.service::report account =
            await std.service::serve(move listener, chosen, move stop, announce_and_echo);
        account as void;
        std.test::fail("the settings cannot run");
    } catch (std.async::start_error failure) {
        return failure == std.async::start_error::scope_full;
    }
    return false;
}

@test
async void validates_its_settings() throws std.error::fault, std.test::failure {
    std.service::options defaults = std.service::options {};
    std.test::equal(defaults.capacity, 64u32);
    std.test::equal(std.time::duration_seconds(defaults.timeout), 30i64);
    std.test::equal(std.time::duration_nanoseconds(defaults.timeout), 0u32);
    std.test::check(defaults.overflow == std.service::overflow::wait, "wait by default");
    std.test::equal(std.service::max_capacity, 1024u32);
    bool oversized = await refuses(options_of(1025u32, 100u32, std.service::overflow::reject));
    std.test::check(oversized == true, "a capacity above the largest");
    bool no_room = await refuses(options_of(0u32, 100u32, std.service::overflow::wait));
    std.test::check(no_room == true, "no slot to wait for");
}

/* ---- serve_all (R-SLIB-SERVICE-0004..0007) ---- */

struct Origins { atomic u32 tcp; atomic u32 unix; };

/* Writes "!", echoes every byte until the end of the client's bytes and counts the listener. */
protected async void echo_any(arc Origins state, std.service::connection connection) throws std.error::fault {
    const Origins* shared = &*state;
    if (connection.source() == 0u32) {
        core::atomic_fetch_add(&shared->tcp, 1u32, core::memory_order::relaxed) as void;
    } else {
        core::atomic_fetch_add(&shared->unix, 1u32, core::memory_order::relaxed) as void;
    }
    u8[1] ready = {33u8};
    task_scope(1) hello { await connection.write_all_from(ready[0usize..1usize]); }
    bytes received = {};
    u8[16] chunk = {};
    bool open = true;
    while (open == true) {
        task_scope(1) input {
            usize count = await connection.read_into(chunk[..]);
            if (count == 0usize) { open = false; }
            received.append(chunk[0usize..count]);
        }
    }
    task_scope(1) output { await connection.write_all_from(received.as_slice()); }
}

/* Sends text over the Unix-domain socket, ends its write direction and returns the reply. */
protected async std.string::string unix_exchange(std.string::string path, std.string::string text)
    throws std.error::fault {
    std.net::unix_stream client = await std.net::unix_connect(path.as_str(), o::none);
    bytes reply = {};
    u8[16] chunk = {};
    task_scope(1) io {
        await std.net::unix_write_all_from(&client, text.as_bytes(), o::none);
        await std.net::unix_shutdown(&client, std.net::shutdown_direction::write, o::none);
    }
    bool open = true;
    while (open == true) {
        task_scope(1) input {
            usize count = await std.net::unix_read_into(&client, chunk[..], o::none);
            if (count == 0usize) { open = false; }
            reply.append(chunk[0usize..count]);
        }
    }
    await std.net::unix_close(move client, o::none);
    return std.string::from_utf8(reply.as_slice());
}

protected async u32 both_clients(std.net::socket_address endpoint, std.string::string path,
                                 std.sync::sender<std.service::stop> stopper) throws std.error::fault {
    u32 answered = 0u32;
    task_scope(1) talk {
        std.string::string reply = await exchange(endpoint, "tcp");
        if (std.bytes::equal(reply.as_bytes(), "!tcp") == true) { answered += 1u32; }
    }
    std.string::string unix_reply = await unix_exchange(move path, std.string::from_str("unix"));
    if (std.bytes::equal(unix_reply.as_bytes(), "!unix") == true) { answered += 1u32; }
    request(&stopper, std.service::stop::drain);
    return answered;
}

protected void add_listener(array<std.service::listener>* listeners, std.service::listener item)
    throws std.alloc::alloc_error {
    try {
        listeners->push(move item);
    } catch (std.array::push_error<std.service::listener> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* Removes a file of the current directory. */
@scoped
protected async void remove_here(str name) throws std.error::fault {
    std.fs::path here = std.fs::path_from_utf8(".");
    std.fs::path entry = std.fs::path_from_utf8(name);
    std.fs::directory root = await std.fs::open_directory(&here);
    await root.remove_file_beneath(&entry);
    await (move root).close();
}

@test
async void serves_tcp_and_unix_listeners_at_once() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::unix_listener local = await std.net::unix_listen("tests_std_service.sock", 4u32, true, o::none);
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::tcp(move listener));
    add_listener(&listeners, std.service::listener::unix(move local));
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.service::health status = std.service::health::create();
    arc Origins state = new arc Origins {.tcp = 0u32, .unix = 0u32};
    task_scope(2) group {
        auto server = std.service::serve_all(move listeners, options_of(4u32, 5000u32, std.service::overflow::wait),
                                             move stop, status.share(), std.arc::clone(&state), echo_any);
        auto client = both_clients(endpoint, std.string::from_str("tests_std_service.sock"), move stopper);
        u32 answered = await move client;
        std.service::report account = await move server;
        std.test::equal(answered, 2u32);
        std.test::equal(account.accepted, 2u64);
        std.test::equal(account.completed, 2u64);
        std.test::equal(account.failed, 0u64);
    }
    const Origins* counted = &*state;
    std.test::equal(core::atomic_load(&counted->tcp, core::memory_order::relaxed), 1u32);
    std.test::equal(core::atomic_load(&counted->unix, core::memory_order::relaxed), 1u32);
    std.test::check(status.serving() == false, "health after the stop");
    std.test::equal(status.accepted(), 2u64);
    std.test::equal(status.active(), 0u32);
    task_scope(1) cleanup { await remove_here("tests_std_service.sock"); }
}

/* Reads from a connection whose client sends nothing: the idle timeout ends the read. */
protected async void wait_for_bytes(arc Origins state, std.service::connection connection) throws std.error::fault {
    drop state;
    u8[4] chunk = {};
    task_scope(1) input {
        usize count = await connection.read_into(chunk[..]);
        count as void;
    }
}

protected async usize idle_client(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    usize count = await visit(endpoint);
    request(&stopper, std.service::stop::drain);
    return count;
}

@test
async void closes_idle_connections() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::tcp(move listener));
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.service::options settings = options_of(4u32, 5000u32, std.service::overflow::wait);
    settings.idle_timeout = o::some(std.time::duration_from_parts(0i64, 100000000u32));
    task_scope(2) group {
        auto server = std.service::serve_all(move listeners, settings, move stop, std.service::health::create(),
                                             new arc Origins {.tcp = 0u32, .unix = 0u32}, wait_for_bytes);
        auto client = idle_client(endpoint, move stopper);
        usize received = await move client;
        std.service::report account = await move server;
        std.test::equal(received, 0usize);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.failed, 1u64);
    }
}

/* stop_and_return for serve_all. */
protected async void stop_and_return_any(arc Stopper state, std.service::connection connection)
    throws std.error::fault {
    request(&state->sender, std.service::stop::drain);
    drop connection;
}

/* stops_with_its_handler for serve_all. */
protected async bool all_stop_with_their_handler() throws std.error::fault {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::tcp(move listener));
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Stopper state = new arc Stopper {.sender = move stopper};
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit = std.time::instant_add(now, std.time::duration_from_seconds(5i64));
    bool exact = false;
    task_scope(2) group {
        auto server = std.service::serve_all(move listeners,
                                             options_of(1u32, 30000u32, std.service::overflow::wait),
                                             move stop, std.service::health::create(), move state,
                                             stop_and_return_any);
        auto client = visit(endpoint);
        select (group) {
        case std.service::report account = await move server:
            exact = account.accepted == 1u64 && account.completed == 1u64 && account.cancelled == 0u64;
        case until (limit): break;
        }
        group.cancel_all();
    }
    return exact;
}

/* P4.1-5 for serve_all. */
@test
async void serve_all_counts_a_handler_that_requests_the_stop() throws std.error::fault, std.test::failure {
    for (u32 index = 0u32; index < 100u32; index += 1u32) {
        bool exact = await all_stop_with_their_handler();
        std.test::check(exact == true, "the request and the outcome both count");
    }
}

/* Raises SIGTERM once the service reports that it serves. */
protected async void terminate_when_serving(std.service::health status) throws std.error::fault {
    while (status.serving() == false) {
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    }
    std.signal::raise(std.signal::kind::terminate);
}

@test
async void stops_on_a_signal() throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await open_listener();
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::tcp(move listener));
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.service::options settings = options_of(4u32, 5000u32, std.service::overflow::wait);
    settings.stop_on_signals = true;
    std.service::health status = std.service::health::create();
    task_scope(2) group {
        auto server = std.service::serve_all(move listeners, settings, move stop, status.share(),
                                             new arc Origins {.tcp = 0u32, .unix = 0u32}, wait_for_bytes);
        auto signaller = terminate_when_serving(status.share());
        await move signaller;
        std.service::report account = await move server;
        std.test::equal(account.accepted, 0u64);
    }
    drop stopper;
    std.test::check(status.serving() == false, "stopped by the signal");
}
