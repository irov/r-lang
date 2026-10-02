module example.keystore.main;
import std.console;
import example.calculator.common::{Usage};
import example.keystore.store::{Command, Full, Record, Step, Store, arity, run};
import example.keystore.backends::{Slots, Log};

// The first argument selects the backend; every command then runs through `dyn(Store)*`.
enum Backend { slots, log };

struct CommandResponse { std.string::string output; i32 status; };

// The session owns the backend the configuration chose at startup as `own dyn(Store)*`
// (Core R-TYPE-0055): its type is fixed only when the program runs, and dropping the session
// drops that backend once.
struct Session { own dyn(Store)* backend; };

Session open(Backend backend) {
    if (backend == Backend::slots) {
        own Slots* slots = new Slots {};
        return Session {.backend = move slots};
    }
    own Log* log = new Log {.records = std.array::create::<Record>()};
    return Session {.backend = move log};
}

// One non-generic `run` serves both backends through the session's `dyn(Store)*`.
void execute(Backend backend, const Step[] plan, std.string::string* output)
    throws Full, std.array::push_error<Record>, std.alloc::alloc_error {
    Session session = open(backend);
    run(&session.backend, plan, output);
}

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};
    try {
        usize count = len(arguments);
        if (count < 2usize) {
            response.output = std.string::from_str("keystore slots|log COMMANDS...\nput KEY VALUE | get KEY | remove KEY | count | backend\n");
        } else {
            o<Backend> selected = core::enum_from_name::<Backend>(arguments[1]);
            Backend backend = Backend::slots;
            switch (selected) {
            case variant o::some(value): backend = *value; break;
            case variant o::none: throw Usage { .message = "unknown keystore backend" };
            }
            array<Step> steps = std.array::create::<Step>();
            usize cursor = 2usize;
            while (cursor < count) {
                o<Command> parsed = core::enum_from_name::<Command>(arguments[cursor]);
                Command command = Command::count;
                switch (parsed) {
                case variant o::some(value): command = *value; break;
                case variant o::none: throw Usage { .message = "unknown keystore command" };
                }
                usize operands = arity(command);
                cursor += 1usize;
                throw (count - cursor < operands) Usage { .message = "missing command operand" };
                u32 key = 0u32;
                u32 value = 0u32;
                if (operands > 0usize) { key = std.convert::parse_u32(arguments[cursor], 10u32); }
                if (operands > 1usize) { value = std.convert::parse_u32(arguments[cursor + 1usize], 10u32); }
                cursor += operands;
                steps.push(Step { .command = command, .key = key, .value = value });
            }
            const Step[] plan = steps.as_slice();
            execute(backend, plan, &response.output);
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (Full failure) {
        u32 capacity = failure.capacity;
        response.output = f"store is full: capacity {capacity}\n"; response.status = 65;
    } catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.array::push_error<Record> failure) { return 71; }
    catch (std.array::push_error<Step> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
