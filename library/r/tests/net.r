module tests.std.net;
import std.test;
import std.net;

// The tests of the R part of std.net (Library R-SLIB-NET-0011): tcp_connect_any tries its
// candidates in order and reports the error of the last attempt, an empty list or a passed
// deadline, and tcp_connect_name resolves a name and connects to its addresses. Every
// connection stays on the loopback interface. Run in test mode (Core R-FUNC-0025).

protected std.net::socket_address loopback(u16 port) throws std.net::address_error {
    return std.net::socket_address {.address = std.net::parse_ip("127.0.0.1"), .port = port,
                                    .scope_id = 0u32};
}

protected std.net::listen_options settings() {
    return std.net::listen_options {.backlog = 8u32, .reuse_address = true, .v6_only = false};
}

/* A loopback address that refuses connections: the one of a listener that has closed. */
protected async std.net::socket_address refusing_endpoint()
    throws std.net::net_error, std.net::address_error, std.async::start_error {
    std.net::socket_address any_port = loopback(0u16);
    std.net::tcp_listener closed = await any_port.listen(settings());
    std.net::socket_address endpoint = closed.local_address();
    await (move closed).close();
    return endpoint;
}

protected void add(array<std.net::socket_address>* chosen, std.net::socket_address item)
    throws std.alloc::alloc_error {
    try {
        std.array::push(chosen, item);
    } catch (std.array::push_error<std.net::socket_address> failure) {
        failure as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected array<std.net::socket_address> two(std.net::socket_address first,
                                             std.net::socket_address second)
    throws std.alloc::alloc_error {
    array<std.net::socket_address> chosen = std.array::create::<std.net::socket_address>();
    add(&chosen, first);
    add(&chosen, second);
    return move chosen;
}

protected array<std.net::socket_address> three(std.net::socket_address first,
                                               std.net::socket_address second,
                                               std.net::socket_address third)
    throws std.alloc::alloc_error {
    array<std.net::socket_address> chosen = two(first, second);
    add(&chosen, third);
    return move chosen;
}

/* The failure of tcp_connect_any, which the candidates shall cause. */
protected async std.net::net_error failure_of(array<std.net::socket_address> candidates)
    throws std.async::start_error, std.test::failure, std.alloc::alloc_error {
    try {
        std.net::tcp_stream unexpected = await std.net::tcp_connect_any(move candidates);
        (move unexpected) as void;
        std.test::fail("no candidate accepts the connection");
    } catch (std.net::net_error failure) {
        return failure;
    }
    return std.net::net_error {.code = std.net::error_code::other, .native_code = -1i64};
}

/* The code of the failure of tcp_connect_name, which the host and port shall cause. */
@scoped
protected async std.net::error_code name_failure_of(str host, u16 port)
    throws std.async::start_error, std.test::failure, std.alloc::alloc_error {
    try {
        task_scope(1) scope {
            std.net::tcp_stream unexpected = await std.net::tcp_connect_name(host, port);
            (move unexpected) as void;
        }
        std.test::fail("the name cannot be connected");
    } catch (std.net::net_error failure) {
        return failure.code;
    }
    return std.net::error_code::other;
}

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

@test
async void connects_to_the_first_candidate_that_accepts()
    throws std.error::fault, std.test::failure {
    std.net::socket_address refused = await refusing_endpoint();
    std.net::socket_address any_port = loopback(0u16);
    std.net::tcp_listener first = await any_port.listen(settings());
    std.net::tcp_listener second = await any_port.listen(settings());
    std.net::socket_address first_end = first.local_address();
    std.net::socket_address second_end = second.local_address();
    std.net::tcp_stream client =
        await std.net::tcp_connect_any(three(refused, first_end, second_end));
    std.net::socket_address peer = std.net::tcp_peer_address(&client);
    std.test::equal(peer.port, first_end.port);
    std.net::tcp_connection accepted = await first.accept();
    std.net::socket_address local = std.net::tcp_local_address(&client);
    std.test::equal(accepted.peer.port, local.port);
    std.string::string message = std.string::from_str("ping over loopback");
    bytes received = {};
    usize count = 0usize;
    task_scope(1) io {
        await std.net::tcp_write_all_from(&client, message);
        await std.net::tcp_shutdown(&client, std.net::shutdown_direction::write);
        count += await read_all(&accepted.stream, &received);
    }
    std.test::equal(count, 18usize);
    std.test::check(std.bytes::equal(received.as_slice(), "ping over loopback"), "the message");
    await (move client).close();
    await (move first).close();
    await (move second).close();
}

@test
async void an_empty_list_is_an_invalid_address() throws std.error::fault, std.test::failure {
    array<std.net::socket_address> none = std.array::create::<std.net::socket_address>();
    std.net::net_error failure = await failure_of(move none);
    std.test::check(failure.code == std.net::error_code::invalid_address, "invalid_address");
    std.test::equal(failure.native_code, 0i64);
}

@test
async void reports_the_error_of_the_last_attempt() throws std.error::fault, std.test::failure {
    std.net::socket_address refused = await refusing_endpoint();
    std.net::socket_address scoped = std.net::socket_address {
        .address = std.net::parse_ip("127.0.0.1"), .port = refused.port, .scope_id = 1u32};
    std.net::net_error refused_last = await failure_of(two(scoped, refused));
    std.test::check(refused_last.code == std.net::error_code::connection_refused,
                    "the refusal of the last candidate");
    std.net::net_error invalid_last = await failure_of(two(refused, scoped));
    std.test::check(invalid_last.code == std.net::error_code::invalid_address,
                    "the invalid scope of the last candidate");
    std.test::equal(invalid_last.native_code, 0i64);
}

@test
async void a_passed_deadline_ends_the_connection() throws std.error::fault, std.test::failure {
    std.net::socket_address any_port = loopback(0u16);
    std.net::tcp_listener listener = await any_port.listen(settings());
    std.net::socket_address endpoint = listener.local_address();
    std.time::instant past = std.time::monotonic_now();
    array<std.net::socket_address> bounded = two(endpoint, endpoint);
    deadline (past) {
        std.net::net_error failure = await failure_of(move bounded);
        std.test::check(failure.code == std.net::error_code::timed_out, "timed_out");
    }
    await (move listener).close();
}

@test
async void connects_by_name() throws std.error::fault, std.test::failure {
    std.net::socket_address any_port = loopback(0u16);
    std.net::tcp_listener listener = await any_port.listen(settings());
    std.net::socket_address endpoint = listener.local_address();
    std.time::instant later = std.time::monotonic_now().add(std.time::duration_from_seconds(10i64));
    deadline (later) {
        task_scope(1) scope {
            std.net::tcp_stream named = await std.net::tcp_connect_name("localhost", endpoint.port);
            std.net::socket_address peer = std.net::tcp_peer_address(&named);
            std.test::equal(peer.port, endpoint.port);
            std.net::tcp_connection accepted = await listener.accept();
            (move accepted) as void;
            await (move named).close();
        }
    }
    task_scope(1) scope {
        std.net::tcp_stream numeric = await std.net::tcp_connect_name("127.0.0.1", endpoint.port);
        std.net::socket_address peer = std.net::tcp_peer_address(&numeric);
        std.test::equal(peer.port, endpoint.port);
        std.net::tcp_connection accepted = await listener.accept();
        (move accepted) as void;
        await (move numeric).close();
    }
    await (move listener).close();
}

@test
async void reports_name_failures() throws std.error::fault, std.test::failure {
    std.net::socket_address refused = await refusing_endpoint();
    std.net::error_code invalid = std.net::error_code::invalid_name;
    task_scope(1) scope {
        std.test::check(await name_failure_of("bad..name", 80u16) == invalid, "an empty label");
        std.test::check(await name_failure_of("under_score.example", 80u16) == invalid,
                        "an underscore");
        std.test::check(await name_failure_of("-leading.example", 80u16) == invalid,
                        "a leading hyphen");
        std.test::check(await name_failure_of("1.2.3.256", 80u16) == invalid,
                        "a numeric name out of range");
        std.test::check(await name_failure_of("", 80u16) == invalid, "an empty name");
        std.test::check(await name_failure_of("127.0.0.1", refused.port) ==
                            std.net::error_code::connection_refused,
                        "a refused port");
    }
}
