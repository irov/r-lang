module audit.std_net_operations;

std.net::socket_address sync_address(const std.net::tcp_listener* listener)
    throws std.net::net_error {
    std.net::socket_address address = std.net::tcp_listener_local_address(listener);
    return address;
}

task<array<std.net::socket_address> throws std.net::net_error> sync_resolve()
    throws std.async::start_error {
    task<array<std.net::socket_address> throws std.net::net_error> operation =
        std.net::resolve("localhost", 80u16, std.net::family::v4, o::none);
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
            std.net::tcp_listener listener = await std.net::tcp_listen(local, options, o::none);
            std.net::socket_address bound = sync_address(&listener);
            if (bound.port == 0u16) { throw TestAssertionFailed {.code = 2}; }
            std.net::tcp_stream client = await std.net::tcp_connect(bound, o::none);
            std.net::tcp_connection accepted = await std.net::tcp_accept(&listener, o::none);
            std.net::socket_address client_address = std.net::tcp_local_address(&client);
            std.net::socket_address peer = std.net::tcp_peer_address(&client);
            if (peer.port != bound.port || accepted.peer.port != client_address.port) { throw TestAssertionFailed {.code = 10}; }
            bytes first = {};
            std.bytes::append_u8(&first, 65u8);
            std.net::tcp_write_result written = await std.net::tcp_write(&client, move first, o::none);
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
                await std.net::tcp_write_all(&client, move second, o::none);
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
                    await std.net::tcp_read(&accepted.stream, move buffer, o::none);
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
            await std.net::tcp_shutdown(&client, std.net::shutdown_direction::write, o::none);
            bytes end_buffer = {};
            std.bytes::append_u8(&end_buffer, 0u8);
            std.net::tcp_read_result ended =
                await std.net::tcp_read(&accepted.stream, move end_buffer, o::none);
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
            await std.net::tcp_close(move client, o::none);
            await std.net::tcp_listener_close(move listener, o::none);
            std.net::udp_socket sender = await std.net::udp_bind(local, false, o::none);
            std.net::udp_socket receiver = await std.net::udp_bind(local, false, o::none);
            std.net::socket_address destination = std.net::udp_local_address(&receiver);
            std.net::socket_address origin = std.net::udp_local_address(&sender);
            bytes message = {};
            std.bytes::append_u8(&message, 90u8);
            std.net::udp_send_result sent =
                await std.net::udp_send_to(&sender, destination, move message, o::none);
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
                await std.net::udp_receive_from(&receiver, move target, o::none);
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
                await std.net::udp_send_to(&sender, invalid, move rejected, o::none);
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
            await std.net::udp_close(move sender, o::none);
            await std.net::udp_close(move receiver, o::none);
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
