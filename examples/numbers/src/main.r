module example.numbers.main;

import std.console;
import example.numbers.dispatch;
import example.numbers.operation::{Operation};
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("numbers OP TYPE VALUE [VALUE] [RADIX]\nOperations: parse convert c_convert limits checked_add/sub/mul wrapping_add/sub/mul saturating_add/sub/mul\n");
        } else {
            throw (count < 3usize || count > 6usize) Usage { .message = "wrong number of arguments" };
            o<Operation> parsed = core::enum_from_name::<Operation>(arguments[1]);
            Operation operation = Operation::parse;
            switch (parsed) {
            case variant o::some(value): operation = *value; break;
            case variant o::none: throw Usage { .message = "unknown operation" };
            }
            bool binary = operation != Operation::parse && operation != Operation::convert &&
                operation != Operation::c_convert && operation != Operation::limits;
            usize minimum = binary == true ? 5usize : (operation == Operation::limits ? 3usize : 4usize);
            throw (count < minimum || count > minimum + 1usize) Usage { .message = "wrong number of operands" };
            str first = count > 3usize ? arguments[3] : "0";
            str second = binary == true ? arguments[4] : "0";
            u32 radix = 10u32;
            if (count > minimum) { radix = std.convert::parse_u32(arguments[minimum], 10u32); }
            response.output = example.numbers.dispatch::evaluate(operation, arguments[2], first, second, radix);
            response.output.append("\n");
        }
    } catch (Usage failure) {
        response.output = f"{failure.message}\n";
        response.status = 64;
    } catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic();
        response.status = 65;
    } catch (std.convert::range_error failure) {
        std.error::error error = std.error::from_range(failure);
        response.output = error.diagnostic();
        response.status = 65;
    } catch (std.format::format_error failure) {
        std.error::error error = std.error::from_format(failure);
        response.output = error.diagnostic();
        response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
