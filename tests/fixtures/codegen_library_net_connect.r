module test.codegen.library_net_connect;

import std.net;

// R-SLIB-NET-0011 (M22): connection by name. tcp_connect_any tries the candidates in order and
// skips one that refuses; tcp_connect_name resolves localhost and connects to whichever of its
// addresses accepts; an empty candidate array reports invalid_address, and when every attempt
// fails the error of the last one is reported.

std.net::socket_address loopback(u16 port) throws std.net::address_error {
    return std.net::socket_address {.address = std.net::parse_ip("127.0.0.1"), .port = port, .scope_id = 0u32};
}

async i32 expect_failure(array<std.net::socket_address> candidates, std.net::error_code code)
    throws std.async::start_error {
    try {
        std.net::tcp_stream unexpected = await std.net::tcp_connect_any(move candidates);
        (move unexpected) as void;
        return 100;
    } catch (std.net::net_error failure) {
        if (failure.code != code) { return 200; }
        return 0;
    }
}

async i32 main() {
    try {
        std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
        std.net::socket_address any_port = loopback(0u16);
        std.net::tcp_listener closed_listener = await any_port.listen(options, o::none);
        std.net::socket_address refused = closed_listener.local_address();
        await (move closed_listener).close(o::none);
        std.net::tcp_listener listener = await any_port.listen(options, o::none);
        std.net::socket_address endpoint = listener.local_address();
        array<std.net::socket_address> candidates = std.array::create::<std.net::socket_address>();
        std.array::push(&candidates, refused);
        std.array::push(&candidates, endpoint);
        std.net::tcp_stream first = await std.net::tcp_connect_any(move candidates);
        std.net::tcp_connection accepted = await listener.accept(o::none);
        (move accepted) as void;
        await (move first).close(o::none);
        i32 status = 0;
        task_scope(1) scope {
            std.net::tcp_stream named = await std.net::tcp_connect_name("localhost", endpoint.port);
            std.net::tcp_connection second = await listener.accept(o::none);
            (move second) as void;
            await (move named).close(o::none);
        }
        status += await expect_failure(std.array::create::<std.net::socket_address>(),
                                       std.net::error_code::invalid_address);
        array<std.net::socket_address> all_refused = std.array::create::<std.net::socket_address>();
        std.array::push(&all_refused, refused);
        status += await expect_failure(move all_refused, std.net::error_code::connection_refused);
        /* A deadline block bounds every attempt; its passed deadline ends the connection at the
           first candidate. */
        std.time::instant past = std.time::monotonic_now();
        array<std.net::socket_address> bounded = std.array::create::<std.net::socket_address>();
        std.array::push(&bounded, endpoint);
        std.array::push(&bounded, endpoint);
        deadline (past) {
            status += await expect_failure(move bounded, std.net::error_code::timed_out);
        }
        await (move listener).close(o::none);
        return status;
    } catch (std.net::net_error failure) {
        failure as void;
        return 90;
    } catch (std.net::address_error failure) {
        failure as void;
        return 91;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 92;
    } catch (std.array::push_error<std.net::socket_address> failure) {
        failure as void;
        return 93;
    } catch (std.time::time_error failure) {
        failure as void;
        return 94;
    }
}
