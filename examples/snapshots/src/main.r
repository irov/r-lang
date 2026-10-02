module example.snapshots.main;
import std.console;
import example.calculator.common::{Usage};
import example.snapshots.local_ref;
import example.snapshots.atomic_ref;
import std.text;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("snapshots rc|arc shared|unique INITIAL REVISED\n");
        } else {
            throw (count != 5usize) Usage { .message = "expected ownership mode, reader policy and two values" };
            bool shared = std.text::equal_ignore_ascii_case(arguments[2], "shared");
            throw (shared == false && (std.text::equal_ignore_ascii_case(arguments[2], "unique")) == false) Usage { .message = "reader policy must be shared or unique" };
            i32 initial = std.convert::parse_i32(arguments[3], 10u32);
            i32 revised = std.convert::parse_i32(arguments[4], 10u32);
            bool atomic_owner = std.text::equal_ignore_ascii_case(arguments[1], "arc");
            if (atomic_owner == true) { response.output = example.snapshots.atomic_ref::run(initial, revised, shared); }
            else {
                bool local = std.text::equal_ignore_ascii_case(arguments[1], "rc");
                throw (local == false) Usage { .message = "ownership mode must be rc or arc" };
                response.output = example.snapshots.local_ref::run(initial, revised, shared);
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
