module example.jobs.main;
import std.console;
import example.jobs.work;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };
enum Command { dispatch, wait, detach, cancel };
async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("jobs dispatch IDS...\njobs wait|detach|cancel MILLISECONDS TEXT\n");
        } else {
            o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::dispatch;
            switch (parsed) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown jobs command" };
            }
            if (command == Command::dispatch) {
                array<u32> ids = std.array::create::<u32>();
                for (usize index = 2usize; index < count; index += 1usize) {
                    u32 id = std.convert::parse_u32(arguments[index], 10u32);
                    ids.push(id);
                }
                const u32[] view = ids.as_slice();
                response.output = example.jobs.work::dispatch(view);
            } else {
                throw (count != 4usize) Usage { .message = "job needs milliseconds and text" };
                u32 milliseconds = std.convert::parse_u32(arguments[2], 10u32);
                throw (milliseconds > 5000u32) Usage { .message = "job delay is limited to five seconds" };
                std.string::string text = std.string::from_str(arguments[3]);
                bytes data = (move text).into_bytes();
                std.string::string report = await example.jobs.work::supervise(move data, milliseconds,
                    command == Command::cancel, command == Command::detach);
                response.output = move report;
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) { response.output = std.string::from_str("invalid integer\n"); response.status = 65; }
    catch (std.array::push_error<u32> failure) { return 71; }
    catch (std.dict::insert_error<example.jobs.work::JobKey<u32>, u32> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
