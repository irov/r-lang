module example.c_bridge.main;
import std.console;
import example.c_bridge.native;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { target, attachment, inspect, upper, release, bytes, cstring };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            std.string::string help = std.string::from_str("c_bridge target\nc_bridge inspect|upper|release TEXT\nc_bridge bytes|cstring HEX\n");
            await std.console::print(move help);
            return 0;
        }
        o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
        Command command = Command::target;
        switch (parsed) {
        case variant o::some(value): command = *value; break;
        case variant o::none: throw Usage { .message = "unknown bridge command" };
        }
        if (command == Command::target || command == Command::attachment) {
            throw (count != 2usize) Usage { .message = "target takes no arguments" };
            if (command == Command::target) { response.output = example.c_bridge.native::target(); }
            else { response.output = example.c_bridge.native::attachment_status(); }
        } else {
            throw (count != 3usize) Usage { .message = "bridge operation needs text" };
            if (command == Command::bytes || command == Command::cstring) {
                bytes data = example.c_bridge.native::decode_hex(arguments[2]);
                const u8[] source = data.as_slice();
                if (command == Command::bytes) { response.output = example.c_bridge.native::inspect_bytes(source); }
                else {
                    str text = core::validate_utf8(source);
                    response.output = example.c_bridge.native::inspect(text);
                }
            } else {
            if (command == Command::inspect) { response.output = example.c_bridge.native::inspect(arguments[2]); }
            else {
                std.string::string transformed = example.c_bridge.native::transform(arguments[2], command == Command::release);
                response.output = f"{transformed}\n";
            }
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (core::utf8_error failure) { response.status = 65; }
    catch (std.c::string_error failure) {
        std.error::error error = failure.as_error();
        response.output = error.diagnostic(); response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
