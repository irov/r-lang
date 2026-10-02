module example.registers.main;
import std.console;
import example.registers.operations;
import example.registers.operations::{TicketError};
import example.registers.device;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { script, race, device };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            if (len(arguments) == 1usize) {
                std.string::string help = std.string::from_str("registers script INITIAL [add|sub|bit_and|bit_or|bit_xor|exchange VALUE|cas EXPECTED DESIRED]...\nregisters race ITERATIONS\nregisters device INITIAL VALUE...\n");
                await std.console::print(move help);
                return 0;
            }
            throw (len(arguments) < 3usize) Usage { .message = "command needs an initial value or iteration count" };
            o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::script;
            switch (selected) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown register command" };
            }
            u32 initial = std.convert::parse_u32(arguments[2], 10u32);
            usize count = len(arguments);
            switch (command) {
            case Command::script:
                array<std.string::string> commands = std.array::create::<std.string::string>();
                for (usize index = 3usize; index < count; index += 1usize) {
                    std.string::string command_text = std.string::from_str(arguments[index]);
                    commands.push(move command_text);
                }
                response.output = example.registers.operations::script(initial, &commands); break;
            case Command::race:
                throw (count != 3usize || initial > 100000u32) Usage { .message = "race needs up to 100000 iterations" };
                response.output = example.registers.operations::race(initial); break;
            case Command::device:
                array<u32> values = std.array::create::<u32>();
                for (usize index = 3usize; index < count; index += 1usize) {
                    u32 value = std.convert::parse_u32(arguments[index], 10u32);
                    values.push(value);
                }
                const u32[] view = values.as_slice();
                response.output = example.registers.device::run(initial, view); break;
            }
        } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (TicketError failure) { return 70; }
    catch (std.convert::parse_error failure) { return 65; }
    catch (std.array::push_error<std.string::string> failure) { return 71; }
    catch (std.array::push_error<u32> failure) { return 71; }
}
