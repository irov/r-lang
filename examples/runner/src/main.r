module example.runner.main;
import std.console;
import example.runner.operations;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Mode { run, capture, stop };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count < 4usize) {
            response.output = std.string::from_str("runner run|capture|stop WORKING_DIRECTORY EXECUTABLE [ARGUMENT...]\n");
        } else {
            o<Mode> selected = core::enum_from_name::<Mode>(arguments[1]);
            Mode mode = Mode::run;
            switch (selected) {
            case variant o::none: throw Usage { .message = "unknown runner mode" };
            case variant o::some(value): mode = *value; break;
            }
            std.fs::path path = std.fs::path_from_utf8(arguments[3]);
            std.fs::path directory = std.fs::path_from_utf8(arguments[2]);
            std.process::command command = std.process::command_create(&path);
            command.working_directory(&directory);
            bool options = true;
            for (usize index = 4usize; index < count; index += 1usize) {
                str argument = arguments[index];
                if ((options == true) && ((std.bytes::equal(argument, "--")) == true)) { options = false; continue; }
                if ((options == true) && ((std.bytes::equal(argument, "--clear-env")) == true)) {
                    command.clear_environment();
                    continue;
                } if ((options == true) && ((std.bytes::equal(argument, "--env")) == true)) {
                    throw (count - index < 3usize) Usage { .message = "--env needs NAME VALUE" };
                    str name = arguments[index + 1usize];
                    str value = arguments[index + 2usize];
                    command.environment(name, value);
                    index += 2usize;
                    continue;
                } if ((options == true) && ((std.bytes::equal(argument, "--unset")) == true)) {
                    throw (count - index < 2usize) Usage { .message = "--unset needs NAME" };
                    index += 1usize;
                    command.remove_environment(arguments[index]);
                    continue;
                }
                command.arg(argument);
            }
            bool capture = mode == Mode::capture;
            bool stop = mode == Mode::stop;
            std.process::pipe_mode pipe = capture == true ? std.process::pipe_mode::piped
                                                          : std.process::pipe_mode::inherit;
            std.process::stdio policy = { .input = pipe, .output = pipe, .error = pipe };
            if (stop == true) {
                policy.input = std.process::pipe_mode::null_device;
                policy.output = std.process::pipe_mode::null_device;
                policy.error = std.process::pipe_mode::null_device;
            }
            command.set_stdio(policy);
            std.string::string report = await example.runner.operations::execute(move command, capture, stop);
            response.output = move report;
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.fs::path_error failure) { response.output = std.string::from_str("invalid executable or directory path\n"); response.status = 65; }
    catch (std.process::process_error failure) {
        std.error::error error = failure.as_error();
        response.output = error.diagnostic(); response.status = 69;
        std.process::error_code code = failure.code;
        if (code == std.process::error_code::not_found) {
            response.output.append("\nhint: verify the executable and working directory\n");
        }
    } catch (std.io::io_error failure) {
        std.error::error error = failure.as_error();
        response.output = error.diagnostic(); response.status = 74;
    } catch (std.string::string_error failure) { response.output = std.string::from_str("child output is not UTF-8\n"); response.status = 65; }
    catch (std.array::push_error<u8> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
