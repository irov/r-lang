module audit.std_net_sync_starts;

std.net::socket_address start_tcp_listener_local_address(const std.net::tcp_listener* arg0)
    throws std.net::net_error {
    std.net::socket_address result = std.net::tcp_listener_local_address(arg0);
    return result;
}

std.net::socket_address start_tcp_peer_address(const std.net::tcp_stream* arg0)
    throws std.net::net_error {
    std.net::socket_address result = std.net::tcp_peer_address(arg0);
    return result;
}

std.net::socket_address start_udp_local_address(const std.net::udp_socket* arg0)
    throws std.net::net_error {
    std.net::socket_address result = std.net::udp_local_address(arg0);
    return result;
}

task<array<std.net::socket_address> throws std.net::net_error> start_resolve(str arg0, u16 arg1, std.net::family arg2, o<std.time::instant> arg3)
    throws std.async::start_error {
    task<array<std.net::socket_address> throws std.net::net_error> result = std.net::resolve(arg0, arg1, arg2, arg3);
    return move result;
}

task<std.net::tcp_listener throws std.net::net_error> start_tcp_listen(std.net::socket_address arg0, std.net::listen_options arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<std.net::tcp_listener throws std.net::net_error> result = std.net::tcp_listen(arg0, arg1, arg2);
    return move result;
}

task<std.net::tcp_connection throws std.net::net_error> start_tcp_accept(const std.net::tcp_listener* arg0, o<std.time::instant> arg1)
    throws std.async::start_error {
    task<std.net::tcp_connection throws std.net::net_error> result = std.net::tcp_accept(arg0, arg1);
    return move result;
}

task<std.net::tcp_stream throws std.net::net_error> start_tcp_connect(std.net::socket_address arg0, o<std.time::instant> arg1)
    throws std.async::start_error {
    task<std.net::tcp_stream throws std.net::net_error> result = std.net::tcp_connect(arg0, arg1);
    return move result;
}

task<std.net::tcp_read_result> start_tcp_read(const std.net::tcp_stream* arg0, bytes arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<std.net::tcp_read_result> result = std.net::tcp_read(arg0, move arg1, arg2);
    return move result;
}

task<std.net::tcp_write_result> start_tcp_write(const std.net::tcp_stream* arg0, bytes arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<std.net::tcp_write_result> result = std.net::tcp_write(arg0, move arg1, arg2);
    return move result;
}

task<std.net::tcp_write_all_result> start_tcp_write_all(const std.net::tcp_stream* arg0, bytes arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<std.net::tcp_write_all_result> result = std.net::tcp_write_all(arg0, move arg1, arg2);
    return move result;
}

task<void throws std.net::net_error> start_tcp_shutdown(const std.net::tcp_stream* arg0, std.net::shutdown_direction arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<void throws std.net::net_error> result = std.net::tcp_shutdown(arg0, arg1, arg2);
    return move result;
}

task<void throws std.net::net_error> start_tcp_close(std.net::tcp_stream arg0, o<std.time::instant> arg1)
    throws std.async::start_error {
    task<void throws std.net::net_error> result = std.net::tcp_close(move arg0, arg1);
    return move result;
}

task<void throws std.net::net_error> start_tcp_listener_close(std.net::tcp_listener arg0, o<std.time::instant> arg1)
    throws std.async::start_error {
    task<void throws std.net::net_error> result = std.net::tcp_listener_close(move arg0, arg1);
    return move result;
}

task<std.net::udp_socket throws std.net::net_error> start_udp_bind(std.net::socket_address arg0, bool arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<std.net::udp_socket throws std.net::net_error> result = std.net::udp_bind(arg0, arg1, arg2);
    return move result;
}

task<std.net::udp_send_result> start_udp_send_to(const std.net::udp_socket* arg0, std.net::socket_address arg1, bytes arg2, o<std.time::instant> arg3)
    throws std.async::start_error {
    task<std.net::udp_send_result> result = std.net::udp_send_to(arg0, arg1, move arg2, arg3);
    return move result;
}

task<std.net::udp_receive_result> start_udp_receive_from(const std.net::udp_socket* arg0, bytes arg1, o<std.time::instant> arg2)
    throws std.async::start_error {
    task<std.net::udp_receive_result> result = std.net::udp_receive_from(arg0, move arg1, arg2);
    return move result;
}

task<void throws std.net::net_error> start_udp_close(std.net::udp_socket arg0, o<std.time::instant> arg1)
    throws std.async::start_error {
    task<void throws std.net::net_error> result = std.net::udp_close(move arg0, arg1);
    return move result;
}


std.net::socket_address sync_address(const std.net::tcp_listener* listener)
    throws std.net::net_error {
    std.net::socket_address address = start_tcp_listener_local_address(listener);
    return address;
}

task<array<std.net::socket_address> throws std.net::net_error> sync_resolve()
    throws std.async::start_error {
    task<array<std.net::socket_address> throws std.net::net_error> operation =
        start_resolve("localhost", 80u16, std.net::family::v4, o::none);
    return move operation;
}

async i32 main() {
    try {
        try {
            task<array<std.net::socket_address> throws std.net::net_error> resolving = sync_resolve();
            array<std.net::socket_address> addresses = await move resolving;
            if (len(addresses) == 0usize) { throw TestAssertionFailed {.code = 1}; }
            drop addresses;
            std.net::ip_address ip = std.net::parse_ip("127.0.0.1");
            std.net::socket_address local = std.net::socket_address {
                .address = ip, .port = 0u16, .scope_id = 0u32,
            };
            std.net::listen_options options = std.net::listen_options {
                .backlog = 4u32, .reuse_address = true, .v6_only = false,
            };
            std.net::tcp_listener listener = await start_tcp_listen(local, options, o::none);
            std.net::socket_address bound = sync_address(&listener);
            if (bound.port == 0u16) { throw TestAssertionFailed {.code = 2}; }
            std.net::tcp_stream client = await start_tcp_connect(bound, o::none);
            std.net::tcp_connection accepted = await start_tcp_accept(&listener, o::none);
            std.net::socket_address client_address = std.net::tcp_local_address(&client);
            std.net::socket_address peer = start_tcp_peer_address(&client);
            if (peer.port != bound.port || accepted.peer.port != client_address.port) { throw TestAssertionFailed {.code = 10}; }
            bytes first = {};
            std.bytes::append_u8(&first, 65u8);
            std.net::tcp_write_result written = await start_tcp_write(&client, move first, o::none);
            switch (move written) {
                case variant std.net::tcp_write_result::written(move payload):
                    if (payload.count != 1usize || len(payload.buffer) != 1usize) { throw TestAssertionFailed {.code = 11}; }
                    drop payload;
                    break;
                case variant std.net::tcp_write_result::failed(move failure):
                    drop failure;
                    throw TestAssertionFailed {.code = 12};
            }
            bytes second = {};
            std.bytes::append_u8(&second, 66u8);
            std.net::tcp_write_all_result all =
                await start_tcp_write_all(&client, move second, o::none);
            switch (move all) {
                case variant std.net::tcp_write_all_result::written(move returned):
                    if (len(returned) != 1usize) { throw TestAssertionFailed {.code = 13}; }
                    drop returned;
                    break;
                case variant std.net::tcp_write_all_result::failed(move failure):
                    drop failure;
                    throw TestAssertionFailed {.code = 14};
            }
            usize total = 0usize;
            while (total < 2usize) {
                bytes buffer = {};
                std.bytes::append_u8(&buffer, 0u8);
                std.net::tcp_read_result read =
                    await start_tcp_read(&accepted.stream, move buffer, o::none);
                switch (move read) {
                    case variant std.net::tcp_read_result::read(move payload):
                        {
                            const u8[] data = std.array::as_slice(&payload.buffer);
                            if (payload.count != 1usize || data[0] != (65usize + total) as u8) { throw TestAssertionFailed {.code = 15}; }
                        }
                        total += payload.count;
                        drop payload;
                        break;
                    case variant std.net::tcp_read_result::end(move returned):
                        drop returned;
                        throw TestAssertionFailed {.code = 16};
                    case variant std.net::tcp_read_result::failed(move failure):
                        drop failure;
                        throw TestAssertionFailed {.code = 17};
                }
            }
            await start_tcp_shutdown(&client, std.net::shutdown_direction::write, o::none);
            bytes end_buffer = {};
            std.bytes::append_u8(&end_buffer, 0u8);
            std.net::tcp_read_result ended =
                await start_tcp_read(&accepted.stream, move end_buffer, o::none);
            switch (move ended) {
                case variant std.net::tcp_read_result::read(move payload):
                    drop payload;
                    throw TestAssertionFailed {.code = 18};
                case variant std.net::tcp_read_result::end(move returned):
                    if (len(returned) != 1usize) { throw TestAssertionFailed {.code = 19}; }
                    drop returned;
                    break;
                case variant std.net::tcp_read_result::failed(move failure):
                    drop failure;
                    throw TestAssertionFailed {.code = 20};
            }
            drop accepted;
            await start_tcp_close(move client, o::none);
            await start_tcp_listener_close(move listener, o::none);
            std.net::udp_socket sender = await start_udp_bind(local, false, o::none);
            std.net::udp_socket receiver = await start_udp_bind(local, false, o::none);
            std.net::socket_address destination = start_udp_local_address(&receiver);
            std.net::socket_address origin = start_udp_local_address(&sender);
            bytes message = {};
            std.bytes::append_u8(&message, 90u8);
            std.net::udp_send_result sent =
                await start_udp_send_to(&sender, destination, move message, o::none);
            switch (move sent) {
                case variant std.net::udp_send_result::sent(move returned):
                    if (len(returned) != 1usize) { throw TestAssertionFailed {.code = 21}; }
                    drop returned;
                    break;
                case variant std.net::udp_send_result::failed(move failure):
                    drop failure;
                    throw TestAssertionFailed {.code = 22};
            }
            bytes target = {};
            std.bytes::append_u8(&target, 0u8);
            std.net::udp_receive_result received =
                await start_udp_receive_from(&receiver, move target, o::none);
            switch (move received) {
                case variant std.net::udp_receive_result::received(move payload):
                    {
                        const u8[] data = std.array::as_slice(&payload.buffer);
                        if (payload.count != 1usize || data[0] != 90u8 || payload.truncated == true ||
                            payload.peer.port != origin.port) { throw TestAssertionFailed {.code = 23}; }
                    }
                    drop payload;
                    break;
                case variant std.net::udp_receive_result::failed(move failure):
                    drop failure;
                    throw TestAssertionFailed {.code = 24};
            }
            std.net::socket_address invalid = destination;
            invalid.scope_id = 1u32;
            bytes rejected = {};
            std.bytes::append_u8(&rejected, 91u8);
            std.net::udp_send_result failed =
                await start_udp_send_to(&sender, invalid, move rejected, o::none);
            switch (move failed) {
                case variant std.net::udp_send_result::sent(move returned):
                    drop returned;
                    throw TestAssertionFailed {.code = 25};
                case variant std.net::udp_send_result::failed(move failure):
                    {
                        const u8[] data = std.array::as_slice(&failure.buffer);
                        if (failure.error.code != std.net::error_code::invalid_address || data[0] != 91u8) {
                            throw TestAssertionFailed {.code = 26};
                        }
                    }
                    drop failure;
                    break;
            }
            await start_udp_close(move sender, o::none);
            await start_udp_close(move receiver, o::none);
            return 0;
        } catch (std.net::address_error error) {
            error as void; throw TestAssertionFailed {.code = 3};
        } catch (std.net::net_error error) {
            error as void; throw TestAssertionFailed {.code = 4};
        } catch (std.alloc::alloc_error error) {
            error as void; throw TestAssertionFailed {.code = 6};
        } catch (std.async::start_error error) {
            error as void; throw TestAssertionFailed {.code = 5};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
