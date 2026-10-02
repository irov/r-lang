module example.binary.main;

import std.console;
import example.binary.codec;
import example.calculator.common::{Usage};
import std.text;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("binary hash TEXT | packet TEXT SEQUENCE | label TEXT | compare TEXT PATTERN\n");
        } else {
            throw (count < 3usize || count > 4usize) Usage { .message = "wrong number of arguments" };
            bool hash = std.text::equal_ignore_ascii_case(arguments[1], "hash");
            bool packet = std.text::equal_ignore_ascii_case(arguments[1], "packet");
            throw (count != ((packet == true || (std.text::equal_ignore_ascii_case(arguments[1], "compare")) == true) ? 4usize : 3usize)) Usage { .message = "wrong number of operands" };
            if (hash == true) { response.output = example.binary.codec::checksums(arguments[2]); }
            else {
                if (packet == true) {
                    u64 sequence = std.convert::parse_u64(arguments[3], 10u32);
                    response.output = example.binary.codec::packet(arguments[2], sequence);
                } else {
                    if ((std.text::equal_ignore_ascii_case(arguments[1], "label")) == true) { response.output = example.binary.codec::label(arguments[2]); }
                    else {
                        throw ((std.text::equal_ignore_ascii_case(arguments[1], "compare")) == false) Usage { .message = "unknown command" };
                        response.output = example.binary.codec::compare(arguments[2], arguments[3]);
                    }
                }
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.convert::range_error failure) {
        std.error::error error = std.error::from_range(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.bits::read_error failure) {
        response.output = std.string::from_str("invalid packet\n"); response.status = 65;
    } catch (std.bytes::bytes_error failure) {
        response.output = std.string::from_str("invalid label range\n"); response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
