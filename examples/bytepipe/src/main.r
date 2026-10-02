module example.bytepipe.main;
import std.console;
import example.bytepipe.transfer;
import example.bytepipe.transfer::{InvalidUtf8, TooLarge};
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string diagnostic; i32 status; };

enum Mode { plain, shared, text, text_async };

async i32 main(const str[] arguments) {
    CommandResponse response = {.diagnostic = std.string::create(), .status = 0};

    try {
        if (len(arguments) == 1usize) {
            std.string::string help = std.string::from_str("bytepipe plain|shared|text|text_async < input > output\n");
            await std.console::print(move help);
            return 0;
        }
        throw (len(arguments) != 2usize) Usage { .message = "bytepipe needs one mode" };
        o<Mode> selected = core::enum_from_name::<Mode>(arguments[1]);
        Mode mode = Mode::plain;
        switch (selected) {
        case variant o::some(value): mode = *value; break;
        case variant o::none: throw Usage { .message = "unknown bytepipe mode" };
        }
        bytes data = await example.bytepipe.transfer::read_input();
        if (mode == Mode::text) {
            bytes valid = example.bytepipe.transfer::validate(move data);
            data = move valid;
        }
        if (mode == Mode::text_async) {
            std.string::from_bytes_result result = std.string::from_bytes(move data);
            switch (move result) {
            case variant std.string::from_bytes_result::valid(move value):
                data = (move value).into_bytes(); break;
            case variant std.string::from_bytes_result::invalid(move failure):
                throw InvalidUtf8 { .offset = failure.index };
            }
        }
        if (mode == Mode::shared) { await example.bytepipe.transfer::write_shared(move data); }
        else { await example.bytepipe.transfer::write_bytes(move data); }
        return 0;
    } catch (Usage failure) { response.diagnostic = f"{failure.message}\n"; response.status = 64; }
    catch (TooLarge failure) { response.diagnostic = std.string::from_str("input exceeds 1 MiB\n"); response.status = 65; }
    catch (InvalidUtf8 failure) { response.diagnostic = f"invalid UTF-8 at byte {failure.offset}\n"; response.status = 65; }
    catch (std.io::io_error failure) {
        std.io::error_code code = failure.code;
        if (code == std.io::error_code::broken_pipe) { return 74; }
        std.error::error error = failure.as_error();
        response.diagnostic = error.diagnostic(); response.status = 74;
    }
    await example.bytepipe.transfer::diagnostic(core::replace(&response.diagnostic, std.string::create()));
    return response.status;
}
