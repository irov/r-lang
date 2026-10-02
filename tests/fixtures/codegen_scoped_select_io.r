module test.codegen.scoped_select_io;

/* R-STMT-0018: a timer against native I/O. The deadline wins while no datagram arrives; the
   explicit cancel releases the buffer loan only after the backend acknowledges it, and the
   same socket and buffer then serve a receive that wins against a distant deadline. */
std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address {
        .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32};
}

async i32 exchange() throws std.net::address_error, std.net::net_error, std.async::start_error,
    std.alloc::alloc_error, std.time::time_error, std.time::duration_error {
    std.net::socket_address local = loopback();
    std.net::udp_socket sender = await local.bind(false, o::none);
    std.net::udp_socket receiver = await local.bind(false, o::none);
    std.net::socket_address destination = receiver.local_address();
    std.string::string text = std.string::from_str("datagram");
    bytes payload = (move text).into_bytes();
    bytes buffer = std.alloc::bytes(16usize, 0u8);
    i32 status = 0;
    usize received = 0usize;
    std.time::instant soon = std.time::instant_add(std.time::monotonic_now(),
                                                   std.time::duration_from_parts(0i64, 20000000u32));
    task_scope(1) idle {
        auto arrival = std.net::udp_receive_into(&receiver, buffer.as_slice_mut(), o::none);
        select (idle) {
        case std.net::datagram datagram = await move arrival:
            if (datagram.count != 0usize) { status = 1; }
            status += 2;
            break;
        case until (soon):
            break;
        }
        idle.cancel_all();
        await idle.all();
    }
    std.time::instant late = std.time::instant_add(std.time::monotonic_now(),
                                                   std.time::duration_from_seconds(10i64));
    task_scope(2) busy {
        auto arrival = std.net::udp_receive_into(&receiver, buffer.as_slice_mut(), o::none);
        const u8[] message = payload.as_slice();
        await sender.send_from(destination, message, o::none);
        select (busy) {
        case std.net::datagram datagram = await move arrival:
            received += datagram.count;
            break;
        case until (late):
            status += 4;
            break;
        }
    }
    await (move sender).close(o::none);
    await (move receiver).close(o::none);
    if (received != len(payload)) { status += 8; }
    const u8[] got = buffer.as_slice();
    const u8[] expected = payload.as_slice();
    if (std.bytes::equal(got[0usize..received], expected) == false) { status += 16; }
    return status;
}

async i32 main() {
    try {
        try {
            std.time::instant started = std.time::monotonic_now();
            i32 status = await exchange();
            if (status != 0) { throw TestAssertionFailed {.code = status}; }
            std.time::instant finished = std.time::monotonic_now();
            std.time::duration elapsed = finished.duration(started);
            if (elapsed.seconds() >= 10i64) { throw TestAssertionFailed {.code = 40}; }
            return 0;
        } catch (std.net::address_error failure) { throw TestAssertionFailed {.code = 65}; }
        catch (std.net::net_error failure) { throw TestAssertionFailed {.code = 69}; }
        catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 71}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 75}; }
        catch (std.time::time_error failure) { throw TestAssertionFailed {.code = 76}; }
        catch (std.time::duration_error failure) { throw TestAssertionFailed {.code = 77}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
