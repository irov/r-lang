module example.preflight.main;
import std.console;
import example.preflight.packet;
import example.preflight.checks::{Check, check};
import example.calculator.common::{Usage};

struct CommandStorage1 { usize value; };

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };
async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("preflight address|path|utf8|integer|frame|packet TEXT\npreflight boundary TEXT OFFSET\npreflight duration SECONDS NANOSECONDS\npreflight bytes SOURCE_OFFSET\npreflight barrier PARTICIPANTS\npreflight reserve U64_ELEMENTS\npreflight thread|asynchronous|profile\n");
        } else {
            o<Check> parsed = core::enum_from_name::<Check>(arguments[1]);
            Check kind = Check::address;
            switch (parsed) {
            case variant o::some(value): kind = *value; break;
            case variant o::none: throw Usage { .message = "unknown preflight check" };
            }
            str source = "";
            CommandStorage1 state_amount = {.value = 0usize};
            usize arity = 3usize;
            if (kind == Check::thread || kind == Check::asynchronous || kind == Check::profile) { arity = 2usize; }
            if (kind == Check::boundary || kind == Check::duration) { arity = 4usize; }
            throw (count != arity) Usage { .message = "wrong number of preflight operands" };
            if (count >= 3usize) { source = arguments[2]; }
            if (arity == 4usize) { state_amount.value = std.convert::parse_usize(arguments[3], 10u32); }
            if (kind == Check::bytes || kind == Check::barrier || kind == Check::reserve) {
                state_amount.value = std.convert::parse_usize(source, 10u32);
            }
            if (kind == Check::packet) {
                std.string::string owned = std.string::from_str(source);
                std.string::string inspected = await example.preflight.packet::inspect(move owned);
                response.output = move inspected;
            } else {
                response.output = check(kind, source, state_amount.value);
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) { response.output = std.string::from_str("invalid numeric operand\n"); response.status = 65; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
