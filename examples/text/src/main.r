module example.text.main;
import std.console;
import example.calculator.common::{Usage};
import example.text.search;
import example.text.inspect;
import std.text;
import std.regex;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("text find|find-i|all|check|split PATTERN TEXT\ntext from PATTERN TEXT OFFSET\ntext replace PATTERN TEXT REPLACEMENT\ntext escape LITERAL\ntext inspect TEXT NEEDLE\ntext message PREFIX BODY BYTE_LIMIT\n");
        } else {
            throw (count < 3usize || count > 5usize) Usage { .message = "wrong number of arguments" };
            bool escape = std.text::equal_ignore_ascii_case(arguments[1], "escape");
            usize expected = escape == true ? 3usize : (((std.text::equal_ignore_ascii_case(arguments[1], "from")) == true || (std.text::equal_ignore_ascii_case(arguments[1], "replace")) == true || (std.text::equal_ignore_ascii_case(arguments[1], "message")) == true) ? 5usize : 4usize);
            throw (count != expected) Usage { .message = "wrong number of operands" };
            str source = count > 3usize ? arguments[3] : "";
            str extra = count > 4usize ? arguments[4] : "";
            bool inspect = std.text::equal_ignore_ascii_case(arguments[1], "inspect");
            if (inspect == true) { response.output = example.text.inspect::inspect(arguments[2], source); }
            else {
                if ((std.text::equal_ignore_ascii_case(arguments[1], "message")) == true) {
                    usize limit = std.convert::parse_usize(extra, 10u32);
                    response.output = example.text.inspect::message(arguments[2], source, limit);
                } else { response.output = example.text.search::run(arguments[1], arguments[2], source, extra); }
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.regex::error failure) {
        std.regex::error_code code = failure.code;
        constexpr str name = core::enum_name(code);
        response.output = f"regex error {name} at byte {failure.offset}\n";
        response.status = 65;
    }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (core::utf8_error failure) { response.output = std.string::from_str("invalid UTF-8\n"); response.status = 65; }
    catch (std.string::string_error failure) { response.output = std.string::from_str("string conversion failed\n"); response.status = 65; }
    catch (std.string::boundary_error failure) { response.output = std.string::from_str("limit splits a UTF-8 character\n"); response.status = 65; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
