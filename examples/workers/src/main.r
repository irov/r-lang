module example.workers.main;
import std.console;
import example.workers.queue;
import example.workers.queue::{WorkerError};
import example.workers.coordination;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { queue, detached, bounded, scoped, owned, park };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            if (len(arguments) == 1usize) {
                std.string::string help = std.string::from_str("workers queue|detached TEXT...\nworkers bounded CAPACITY TEXT...\nworkers scoped|owned NUMBER...\nworkers park MILLISECONDS\n");
                await std.console::print(move help);
                return 0;
            }
            o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::queue;
            switch (parsed) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown worker command" };
            }
            if (command == Command::park) {
                throw (len(arguments) != 3usize) Usage { .message = "park needs milliseconds" };
                u32 delay = std.convert::parse_u32(arguments[2], 10u32);
                throw (delay > 1000u32) Usage { .message = "park delay is limited to one second" };
                response.output = example.workers.coordination::parked(delay);
            } else {
                if (command == Command::owned) {
                    list<u32> values = std.list::create::<u32>();
                    for (usize index = 2usize; index < len(arguments); index += 1usize) {
                        u32 value = std.convert::parse_u32(arguments[index], 10u32);
                        u32* inserted = std.list::push_back(&values, value); const u32* observed = inserted; observed as void;
                    }
                    fn once u64 sum_values() move(values) {
                        u64 total = 0u64;
                        for (const u32* value in &values) { total += *value as u64; }
                        return total;
                    }
                    task<u64> calculation = example.workers.coordination::execute(move sum_values);
                    std.string::string report = await example.workers.coordination::report_sum(move calculation);
                    response.output = move report;
                } else {
                    if (command == Command::scoped) {
                        array<u32> values = std.array::create::<u32>();
                        for (usize index = 2usize; index < len(arguments); index += 1usize) {
                            u32 value = std.convert::parse_u32(arguments[index], 10u32);
                            values.push(value);
                        }
                        const u32[] view = values.as_slice();
                        response.output = example.workers.coordination::scoped(view);
                    } else {
                        usize first = 2usize;
                        usize capacity = 0usize;
                        if (command == Command::bounded) {
                            throw (len(arguments) < 3usize) Usage { .message = "bounded queue needs capacity" };
                            capacity = std.convert::parse_usize(arguments[2], 10u32);
                            throw (capacity > 4096usize) Usage { .message = "capacity is limited to 4096" };
                            first = 3usize;
                        }
                        array<std.string::string> messages = std.array::create::<std.string::string>();
                        for (usize index = first; index < len(arguments); index += 1usize) {
                            std.string::string message = std.string::from_str(arguments[index]);
                            messages.push(move message);
                        }
                        if (command == Command::bounded) { response.output = example.workers.queue::bounded(move messages, capacity); }
                        else { response.output = example.workers.queue::unbounded(move messages, command == Command::detached); }
                    }
                }
            }
        } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
        catch (WorkerError failure) { response.output = std.string::from_str("worker delivery failed\n"); response.status = 70; }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (std.convert::parse_error failure) { return 65; }
    catch (std.array::push_error<u32> failure) { return 71; }
    catch (std.list::push_error<u32> failure) { return 71; }
    catch (std.array::push_error<std.string::string> failure) { return 71; }
}
