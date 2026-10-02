module example.ipc.main;
import std.console;
import example.ipc.stream;
import example.ipc.datagram;

enum Command { serve, call, notify, collect, signal };

// The output and exit status of a command.
struct Response { std.string::string output; i32 status; };

Response respond(str text, i32 status) throws std.alloc::alloc_error {
    return Response {.output = std.string::from_str(text), .status = status};
}

/* The help without arguments, a usage error with status 64 when the arguments do not name a
   command with its inputs, or none. */
o<Response> usage(const str[] arguments) throws std.alloc::alloc_error, std.convert::parse_error {
    if (len(arguments) == 1usize) {
        return o::some(respond("ipc serve PATH LIMIT\nipc call PATH MESSAGE\nipc notify PATH MESSAGE\nipc collect PATH COUNT CAPACITY\nipc signal COUNT\n", 0));
    }
    o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
    switch (parsed) {
    case variant o::none: return o::some(respond("unknown ipc command\n", 64));
    case variant o::some(command):
        usize required = 4usize;
        if (*command == Command::collect) { required = 5usize; }
        if (*command == Command::signal) { required = 3usize; }
        if (len(arguments) != required) { return o::some(respond("wrong argument count\n", 64)); }
        if (*command == Command::signal) {
            u32 count = std.convert::parse_u32(arguments[2], 10u32);
            if (count == 0u32 || count > 16u32) { return o::some(respond("1 to 16 signals\n", 64)); }
            return o::none;
        }
        if (*command == Command::call || *command == Command::notify) {
            if (len(arguments[3]) > 4000usize) { return o::some(respond("message exceeds 4000 bytes\n", 64)); }
        }
        if (*command == Command::serve || *command == Command::collect) {
            u32 count = std.convert::parse_u32(arguments[3], 10u32);
            if (count == 0u32 || count > 100u32) { return o::some(respond("1 to 100 clients or datagrams\n", 64)); }
        }
        if (*command == Command::collect) {
            if (std.convert::parse_usize(arguments[4], 10u32) > 65536usize) {
                return o::some(respond("capacity exceeds 65536 bytes\n", 64));
            }
        }
    }
    return o::none;
}

Command command_of(str name) {
    o<Command> parsed = core::enum_from_name::<Command>(name);
    Command command = Command::serve;
    switch (parsed) {
    case variant o::some(value): command = *value;
    case variant o::none: break;
    }
    return command;
}

/* Raise user1 `count` times at the program itself and wait for the deliveries; the target may
   merge deliveries that arrive before the wait, so at least one and at most `count` complete it. */
async std.string::string signal(u32 count) throws std.error::fault {
    std.signal::listener user = std.signal::kind::user1.listen();
    std.signal::kind::user1.raise();
    for (u32 index = 1u32; index < count; index += 1u32) {
        std.signal::raise(std.signal::kind::user1);
    }
    u64 delivered = await user.next();
    bool within = delivered >= 1u64 && delivered <= (count as u64);
    return f"user1 raised={count} delivered_within_range={within}\n";
}

/* An invalid number exits with 65, a network or signal failure with 69, any other with 70. */
Response explain(std.error::fault failure) throws std.alloc::alloc_error {
    try {
        throw failure;
    } catch (std.convert::parse_error input) {
        return respond("invalid number\n", 65);
    } catch (std.net::net_error network) {
        std.error::error error = network.as_error();
        std.string::string description = error.diagnostic();
        return Response {.output = f"{description}\n", .status = 69};
    } catch (std.process::process_error raised) {
        std.error::error error = raised.as_error();
        std.string::string description = error.diagnostic();
        return Response {.output = f"{description}\n", .status = 69};
    } catch (std.error::fault other) {
        std.error::error error = std.error::from_fault(other);
        std.string::string description = error.diagnostic();
        description.append("\n");
        return Response {.output = move description, .status = 70};
    }
}

async i32 main(const str[] arguments) {
    Response response = {.output = std.string::create(), .status = 0};
    try {
        o<Response> early = usage(arguments);
        switch (move early) {
        case variant o::some(move answer): response = move answer;
        case variant o::none:
            switch (command_of(arguments[1])) {
            case Command::serve:
                u32 limit = std.convert::parse_u32(arguments[3], 10u32);
                u32 served = await example.ipc.stream::serve(std.string::from_str(arguments[2]), limit);
                response.output = f"served {served}\n";
            case Command::call:
                std.string::string message = std.string::from_str(arguments[3]);
                response.output = await example.ipc.stream::call(std.string::from_str(arguments[2]), move message);
            case Command::notify:
                std.string::string message = std.string::from_str(arguments[3]);
                response.output = await example.ipc.datagram::notify(std.string::from_str(arguments[2]), move message);
            case Command::collect:
                u32 count = std.convert::parse_u32(arguments[3], 10u32);
                usize capacity = std.convert::parse_usize(arguments[4], 10u32);
                response.output = await example.ipc.datagram::collect(std.string::from_str(arguments[2]), count, capacity);
            case Command::signal:
                u32 count = std.convert::parse_u32(arguments[2], 10u32);
                response.output = await signal(count);
            }
        }
    } catch (std.error::fault failure) {
        response = explain(failure);
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
