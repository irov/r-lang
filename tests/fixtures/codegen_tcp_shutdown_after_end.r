module test.codegen.tcp_shutdown_after_end;

import std.net;

/* R-SLIB-NET-0006: a stream whose peer has ended its output shuts down both directions after the
   local end is observed. The read side is already unable to receive, the write side publishes
   its end to the peer, and a repeated shutdown is an idempotent success. Later nonempty reads
   complete as end and later nonempty writes fail with not_connected. */

error Unexpected { i32 code; };

@scoped
async usize drain(const std.net::tcp_stream* stream, o<std.time::instant> until)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    usize total = 0usize;
    while (true) {
        bytes buffer = std.alloc::bytes(64usize, 0u8);
        std.net::tcp_read_result result = await std.net::tcp_read(stream, move buffer, until);
        switch (move result) {
        case variant std.net::tcp_read_result::read(move part):
            total += part.count;
            break;
        case variant std.net::tcp_read_result::end(move returned):
            move returned as void;
            return total;
        case variant std.net::tcp_read_result::failed(move failure):
            throw failure.error;
        }
    }
}

@scoped
async void send(const std.net::tcp_stream* stream, o<std.time::instant> until)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    bytes message = std.alloc::bytes(5usize, 7u8);
    std.net::tcp_write_all_result sent = await std.net::tcp_write_all(stream, move message, until);
    switch (move sent) {
    case variant std.net::tcp_write_all_result::written(move returned):
        move returned as void;
        break;
    case variant std.net::tcp_write_all_result::failed(move failure):
        throw failure.error;
    }
}

/* The accepting side after its peer ended: both directions, twice, then read and write. */
@scoped
async void finish(const std.net::tcp_stream* stream, o<std.time::instant> until)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error, Unexpected {
    task_scope(1) group {
        if (await drain(stream, until) != 5usize) {
            throw Unexpected {.code = 1};
        }
    }
    await std.net::tcp_shutdown(stream, std.net::shutdown_direction::both, until);
    await std.net::tcp_shutdown(stream, std.net::shutdown_direction::both, until);
    await std.net::tcp_shutdown(stream, std.net::shutdown_direction::read, until);
    task_scope(1) group {
        if (await drain(stream, until) != 0usize) {
            throw Unexpected {.code = 2};
        }
    }
    bytes late = std.alloc::bytes(3usize, 1u8);
    std.net::tcp_write_result written = await std.net::tcp_write(stream, move late, until);
    switch (move written) {
    case variant std.net::tcp_write_result::written(move part):
        move part as void;
        throw Unexpected {.code = 3};
    case variant std.net::tcp_write_result::failed(move failure):
        if (failure.error.code != std.net::error_code::not_connected) {
            throw Unexpected {.code = 4};
        }
        break;
    }
}

async std.net::tcp_listener open(o<std.time::instant> until)
    throws std.net::address_error, std.net::net_error, std.async::start_error {
    std.net::ip_address ip = std.net::parse_ip("127.0.0.1");
    std.net::socket_address local =
        std.net::socket_address {.address = ip, .port = 0u16, .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options, until);
}

/* The peer half-closes and later reads the end the accepting side publishes. */
async void half_close()
    throws std.net::address_error, std.net::net_error, std.async::start_error,
        std.alloc::alloc_error, Unexpected {
    o<std.time::instant> until = o::none;
    std.net::tcp_listener listener = await open(until);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect(until);
    std.net::tcp_connection connection = await listener.accept(until);
    await (move listener).close(until);
    task_scope(1) group {
        await send(&client, until);
    }
    await std.net::tcp_shutdown(&client, std.net::shutdown_direction::write, until);
    task_scope(1) group {
        await finish(&connection.stream, until);
    }
    task_scope(1) group {
        if (await drain(&client, until) != 0usize) {
            throw Unexpected {.code = 5};
        }
    }
    await (move client).close(until);
}

/* The peer closes completely before the accepting side shuts down. */
async void full_close()
    throws std.net::address_error, std.net::net_error, std.async::start_error,
        std.alloc::alloc_error, Unexpected {
    o<std.time::instant> until = o::none;
    std.net::tcp_listener listener = await open(until);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect(until);
    std.net::tcp_connection connection = await listener.accept(until);
    await (move listener).close(until);
    task_scope(1) group {
        await send(&client, until);
    }
    await (move client).close(until);
    task_scope(1) group {
        await finish(&connection.stream, until);
    }
}

async i32 main() {
    try {
        await half_close();
        await full_close();
        return 0;
    } catch (Unexpected failure) {
        return failure.code;
    } catch (std.net::net_error failure) {
        failure as void;
        return 90;
    } catch (std.net::address_error failure) {
        failure as void;
        return 91;
    } catch (std.async::start_error failure) {
        failure as void;
        return 92;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 93;
    }
}
