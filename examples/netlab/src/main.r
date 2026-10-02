module example.netlab.main;
import std.console;
import example.netlab.tcp;
import example.netlab.tcp::{Response};
import example.netlab.handoff;
import example.netlab.udp;
import example.netlab.address;
import example.netlab.tune;
import example.netlab.dial;

enum Command { ip, resolve, tcp, handoff, udp, options, dial };
enum Family { any, v4, v6 };

Response respond(str text, i32 status) throws std.alloc::alloc_error {
    return Response {.output = std.string::from_str(text), .status = status};
}

/* The help without arguments, a usage error with status 64 when the arguments do not name a
   command with its inputs, or none. */
o<Response> usage(const str[] arguments) throws std.alloc::alloc_error, std.convert::parse_error {
    if (len(arguments) == 1usize) {
        return o::some(respond("netlab ip ADDRESS\nnetlab resolve HOST PORT any|v4|v6\nnetlab tcp MESSAGE\nnetlab handoff MESSAGE\nnetlab udp MESSAGE CAPACITY\nnetlab options KEEPALIVE_MS HOP_LIMIT\nnetlab dial HOST\n", 0));
    }
    if (len(arguments) < 3usize) { return o::some(respond("command and input required\n", 64)); }
    o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
    switch (parsed) {
    case variant o::none: return o::some(respond("unknown network command\n", 64));
    case variant o::some(command):
        usize required = 3usize;
        if (*command == Command::resolve) { required = 5usize; }
        if (*command == Command::udp || *command == Command::options) { required = 4usize; }
        if (len(arguments) != required) { return o::some(respond("wrong argument count\n", 64)); }
        if (*command == Command::resolve) {
            o<Family> family = core::enum_from_name::<Family>(arguments[4]);
            switch (family) {
            case variant o::some(value): value as void;
            case variant o::none: return o::some(respond("unknown address family\n", 64));
            }
        }
        if ((*command == Command::tcp || *command == Command::handoff) && len(arguments[2]) > 1048576usize) {
            return o::some(respond("message exceeds 1 MiB\n", 64));
        }
        if (*command == Command::options) {
            if (std.convert::parse_u32(arguments[2], 10u32) > 3600000u32) {
                return o::some(respond("keepalive exceeds one hour\n", 64));
            }
            u32 hops = std.convert::parse_u32(arguments[3], 10u32);
            hops as void;
        }
        if (*command == Command::udp) {
            if (len(arguments[2]) > 60000usize) { return o::some(respond("datagram exceeds 60000 bytes\n", 64)); }
            if (std.convert::parse_usize(arguments[3], 10u32) > 60000usize) {
                return o::some(respond("receive capacity exceeds 60000 bytes\n", 64));
            }
        }
    }
    return o::none;
}

Command command_of(str name) {
    o<Command> parsed = core::enum_from_name::<Command>(name);
    Command command = Command::ip;
    switch (parsed) {
    case variant o::some(value): command = *value;
    case variant o::none: break;
    }
    return command;
}

std.net::family family_of(str name) {
    o<Family> parsed = core::enum_from_name::<Family>(name);
    std.net::family family = std.net::family::any;
    switch (parsed) {
    case variant o::some(value):
        if (*value == Family::v4) { family = std.net::family::v4; }
        if (*value == Family::v6) { family = std.net::family::v6; }
    case variant o::none: break;
    }
    return family;
}

/* The response to a standard failure: an invalid address or number exits with 65, a network
   failure with 69 and any other failure with 70; the rethrow finds which error the root holds. */
Response explain(std.error::fault failure) throws std.alloc::alloc_error {
    try {
        throw failure;
    } catch (std.net::address_error input) {
        bool empty = input.code == std.net::address_error_code::empty;
        return Response {.output = f"invalid address at byte {input.index}, empty={empty}\n", .status = 65};
    } catch (std.convert::parse_error input) {
        return respond("invalid number\n", 65);
    } catch (std.net::net_error network) {
        bool timeout = network.code == std.net::error_code::timed_out;
        std.error::error error = network.as_error();
        std.string::string description = error.diagnostic();
        return Response {.output = f"{description}, timeout={timeout}\n", .status = 69};
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
            case Command::ip:
                std.net::ip_address address = std.net::parse_ip(arguments[2]);
                response.output = address.format_ip();
                response.output.append("\n");
            case Command::resolve:
                u16 port = std.convert::parse_u16(arguments[3], 10u32);
                std.string::string host = std.string::from_str(arguments[2]);
                response.output = await example.netlab.address::resolve(move host, port, family_of(arguments[4]));
            case Command::tcp:
                response = await example.netlab.tcp::deliver(std.string::from_str(arguments[2]));
            case Command::handoff:
                response = await example.netlab.handoff::deliver(std.string::from_str(arguments[2]));
            case Command::udp:
                usize capacity = std.convert::parse_usize(arguments[3], 10u32);
                std.string::string message = std.string::from_str(arguments[2]);
                response.output = await example.netlab.udp::deliver(move message, capacity);
            case Command::options:
                u32 keepalive = std.convert::parse_u32(arguments[2], 10u32);
                u32 hops = std.convert::parse_u32(arguments[3], 10u32);
                response.output = await example.netlab.tune::tune(keepalive, hops);
            case Command::dial:
                task_scope(1) dialing {
                    response.output = await example.netlab.dial::dial(arguments[2]);
                }
            }
        }
    } catch (std.error::fault failure) {
        response = explain(failure);
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
