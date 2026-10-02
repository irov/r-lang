module example.ledger.main;
import std.console;
import example.ledger.accounts;
import example.ledger.accounts::{LedgerError};
import example.ledger.handoff;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { batch, handoff };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            if (len(arguments) == 1usize) {
                std.string::string help = std.string::from_str("ledger batch DELTA...\nledger handoff VALUE\n");
                await std.console::print(move help);
                return 0;
            }
            o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::batch;
            switch (selected) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown ledger command" };
            }
            if (command == Command::batch) {
                array<i32> entries = std.array::create::<i32>();
                for (usize index = 2usize; index < len(arguments); index += 1usize) {
                    i32 delta = std.convert::parse_i32(arguments[index], 10u32);
                    entries.push(delta);
                }
                const i32[] view = entries.as_slice();
                response.output = example.ledger.accounts::batch(view);
            } else {
                throw (len(arguments) != 3usize) Usage { .message = "handoff needs one value" };
                i64 message = std.convert::parse_i64(arguments[2], 10u32);
                response.output = example.ledger.handoff::run(message);
            }
        } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
        catch (LedgerError failure) { response.output = std.string::from_str("ledger synchronization failed\n"); response.status = 70; }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (std.convert::parse_error failure) { return 65; }
    catch (std.array::push_error<i32> failure) { return 71; }
}
