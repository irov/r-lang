module example.buffer_compare.main;
import std.console;
import example.calculator.common::{Usage};
import example.buffer_compare.compare::{Pair, compare};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("buffer_compare EXPECTED CANDIDATE\n");
        } else {
            throw (count != 3usize) Usage { .message = "expected two input strings" };
            response.output = compare(arguments[1], arguments[2]);
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.bytes::bytes_error failure) {
        response.output = std.string::from_str("input buffer length mismatch\n"); response.status = 65;
    } catch (std.alloc::new_error<Pair> failure) {
        // The error owns both rejected buffers and destroys them on this return path.
        return 71;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
