module example.warehouse.main;
import std.console;
import example.calculator.common::{Usage};
import example.warehouse.stock::{Command, arity, apply};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        dict<i32, i32> inventory = std.dict::with_capacity::<i32, i32>(8usize);
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("warehouse COMMANDS...\nset SKU QUANTITY | add SKU DELTA | get|has|remove SKU\nreserve ADDITIONAL | show | clear | reset\n");
        } else {
            usize cursor = 1usize;
            while (cursor < count) {
                o<Command> parsed = core::enum_from_name::<Command>(arguments[cursor]);
                Command command = Command::show;
                switch (parsed) {
                case variant o::some(value): command = *value; break;
                case variant o::none: throw Usage { .message = "unknown warehouse command" };
                }
                usize operands = arity(command);
                cursor += 1usize;
                throw (count - cursor < operands) Usage { .message = "missing command operand" };
                i32 key = 0;
                i32 amount = 0;
                if (operands > 0usize) { key = std.convert::parse_i32(arguments[cursor], 10u32); }
                if (operands > 1usize) { amount = std.convert::parse_i32(arguments[cursor + 1usize], 10u32); }
                cursor += operands;
                apply(&inventory, command, key, amount, &response.output);
            }
        }
    drop inventory;
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.dict::insert_error<i32, i32> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
