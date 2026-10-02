module example.playlist.main;
import std.console;
import example.calculator.common::{Usage};
import example.playlist.editor::{Command, arity, apply};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        list<i32> tracks = std.list::create::<i32>();
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("playlist COMMANDS...\nappend|prepend|set_first|set_last VALUE\nbefore|after|set INDEX VALUE\nget|remove INDEX\nfirst|last|pop_first|pop_last|clear|show\n");
        } else {
            usize cursor = 1usize;
            while (cursor < count) {
                o<Command> parsed = core::enum_from_name::<Command>(arguments[cursor]);
                Command command = Command::show;
                switch (parsed) {
                case variant o::some(value): command = *value; break;
                case variant o::none: throw Usage { .message = "unknown playlist command" };
                }
                usize operands = arity(command);
                cursor += 1usize;
                throw (count - cursor < operands) Usage { .message = "missing command operand" };
                usize index = 0usize;
                i32 value = 0;
                bool indexed = command == Command::get || command == Command::remove || operands == 2usize;
                if (indexed == true) { index = std.convert::parse_usize(arguments[cursor], 10u32); }
                if (operands == 2usize) { value = std.convert::parse_i32(arguments[cursor + 1usize], 10u32); }
                else {
                    if (operands == 1usize && indexed == false) { value = std.convert::parse_i32(arguments[cursor], 10u32); }
                }
                cursor += operands;
                apply(&tracks, command, index, value, &response.output);
            }
        }
    drop tracks;
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.list::push_error<i32> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
