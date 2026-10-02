module test.codegen.deadline_blocks;

/* R-STMT-0019: a deadline block bounds the standard operations started inside it, also by the
   tasks it starts, never extends the deadline it narrows and is restored on every exit; a call
   may omit the trailing deadline argument of a standard operation (R-SLIB-ASYNC-0008). */
error Stop { };

std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address {
        .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32};
}

// Binds and closes a socket without deadline arguments: 0 bound, 1 timed out.
async i32 bind_status() {
    try {
        std.net::socket_address local = loopback();
        std.net::udp_socket socket = await local.bind(false);
        await (move socket).close();
        return 0;
    } catch (std.net::net_error failure) {
        if (failure.code == std.net::error_code::timed_out) { return 1; }
        return 2;
    } catch (std.net::address_error failure) { return 3; }
    catch (std.async::start_error failure) { return 4; }
}

// A synchronous function starts the operation for the task that calls it.
task<std.net::udp_socket throws std.net::net_error> start_bind(std.net::socket_address local)
    throws std.async::start_error {
    return std.net::udp_bind(local, false);
}

async i32 inherited(std.time::instant past) throws std.async::start_error {
    deadline (past) {
        i32 status = await bind_status();
        if (status != 1) { return 11; }
    }
    return 0;
}

async i32 started_before(std.time::instant past) throws std.async::start_error {
    task<i32> early = bind_status();
    deadline (past) {
        i32 status = await move early;
        if (status != 0) { return 12; }
    }
    return 0;
}

async i32 narrowed(std.time::instant past, std.time::instant future)
    throws std.async::start_error, std.net::address_error, std.net::net_error {
    std.net::socket_address local = loopback();
    deadline (past) {
        try {
            std.net::udp_socket socket = await local.bind(false, o::some(future));
            await (move socket).close();
            throw Stop { };
        } catch (Stop failure) { return 13; }
        catch (std.net::net_error failure) {
            if (failure.code != std.net::error_code::timed_out) { return 14; }
        }
    }
    return 0;
}

async i32 nested(std.time::instant past, std.time::instant future) throws std.async::start_error {
    deadline (past) {
        deadline (future) {
            i32 status = await bind_status();
            if (status != 1) { return 15; }
        }
    }
    return 0;
}

async i32 restored(std.time::instant past) throws std.async::start_error {
    for (i32 index = 0; index < 3; index += 1) {
        deadline (past) {
            if (index == 1) { break; }
        }
    }
    i32 after_break = await bind_status();
    if (after_break != 0) { return 16; }
    try {
        deadline (past) { throw Stop { }; }
    } catch (Stop failure) { }
    i32 after_throw = await bind_status();
    if (after_throw != 0) { return 17; }
    deadline (o::none) {
        i32 unbounded = await bind_status();
        if (unbounded != 0) { return 18; }
    }
    return 0;
}

async u32 sleeper(std.time::duration delay) throws std.time::time_error, std.async::start_error {
    await std.time::sleep_for(delay);
    return 7u32;
}

async i32 grouped(std.time::instant past, std.time::instant future)
    throws std.async::start_error, std.time::time_error {
    i32 status = 0;
    task_scope(2) group {
        auto slow = sleeper(std.time::duration_from_seconds(30i64));
        deadline (past) {
            o<usize> ready = await group.first_until(future, &slow);
            switch (ready) {
            case variant o::some(index): status = 19; break;
            case variant o::none: break;
            }
            auto member = bind_status();
            i32 member_status = await move member;
            if (member_status != 1) { status = 20; }
        }
        group.cancel_all();
        await group.all();
    }
    return status;
}

async i32 synchronous_start(std.time::instant past)
    throws std.async::start_error, std.net::address_error, std.net::net_error {
    std.net::socket_address local = loopback();
    deadline (past) {
        try {
            task<std.net::udp_socket throws std.net::net_error> started = start_bind(local);
            std.net::udp_socket socket = await move started;
            await (move socket).close();
            throw Stop { };
        } catch (Stop failure) { return 21; }
        catch (std.net::net_error failure) {
            if (failure.code != std.net::error_code::timed_out) { return 22; }
        }
    }
    return 0;
}

// A scoped operation that would succeed at once without a deadline.
async i32 scoped(std.time::instant past) throws std.async::start_error, std.net::address_error,
    std.net::net_error, std.alloc::alloc_error {
    std.net::socket_address local = loopback();
    std.net::udp_socket socket = await local.bind(false);
    std.net::socket_address destination = socket.local_address();
    bytes payload = std.alloc::bytes(4usize, 7u8);
    i32 status = 0;
    task_scope(1) group {
        deadline (past) {
            try {
                const u8[] message = payload.as_slice();
                await socket.send_from(destination, message);
                throw Stop { };
            } catch (Stop failure) { status = 24; }
            catch (std.net::net_error failure) {
                if (failure.code != std.net::error_code::timed_out) { status = 25; }
            }
        }
    }
    await (move socket).close();
    return status;
}

// Timers are not bounded by a deadline block.
async i32 timers(std.time::instant past) throws std.async::start_error, std.time::time_error {
    deadline (past) {
        await std.time::sleep_for(std.time::duration_from_seconds(0i64));
    }
    return 0;
}

async i32 main() {
    std.time::instant now = std.time::monotonic_now();
    std.time::instant past = now.add(std.time::duration_from_seconds(-1i64));
    std.time::instant future = now.add(std.time::duration_from_seconds(60i64));
    i32 first = await inherited(past);
    i32 second = await started_before(past);
    i32 third = await narrowed(past, future);
    i32 fourth = await nested(past, future);
    i32 fifth = await restored(past);
    i32 sixth = await grouped(past, future);
    i32 seventh = await synchronous_start(past);
    i32 eighth = await scoped(past);
    i32 ninth = await timers(past);
    return first + second + third + fourth + fifth + sixth + seventh + eighth + ninth;
}
