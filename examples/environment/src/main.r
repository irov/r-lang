module example.environment.main;
import std.console;
import example.environment.inspect;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { get, set, unset, summary, arguments };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("environment get NAME\nenvironment set NAME VALUE\nenvironment unset NAME\nenvironment summary\nenvironment arguments [VALUE...]\n");
        } else {
            o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::summary;
            switch (selected) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown environment command" };
            }
            switch (command) {
            case Command::get:
                throw (count != 3usize) Usage { .message = "get needs a name" };
                response.output = example.environment.inspect::get(arguments[2]); break;
            case Command::set:
                throw (count != 4usize) Usage { .message = "set needs a name and value" };
                std.env::set(arguments[2], arguments[3]);
                response.output = example.environment.inspect::get(arguments[2]); break;
            case Command::unset:
                throw (count != 3usize) Usage { .message = "unset needs a name" };
                std.env::remove(arguments[2]);
                response.output = example.environment.inspect::get(arguments[2]); break;
            case Command::summary:
                throw (count != 2usize) Usage { .message = "summary takes no operands" };
                response.output = example.environment.inspect::summary(); break;
            case Command::arguments:
                response.output = example.environment.inspect::arguments(); break;
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.env::env_error failure) {
        std.env::error_code code = failure.code;
        if (code == std.env::error_code::invalid_name) {
            response.output = std.string::from_str("invalid environment variable name\n");
        } else {
            std.error::error error = std.env::as_error(failure);
            constexpr str name = error.name();
            std.string::string details = error.diagnostic();
            response.output = f"{name}: {details}\n";
        }
        response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
