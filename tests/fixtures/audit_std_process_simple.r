module audit.std_process_simple;

i32 configure_command() throws std.fs::path_error, std.process::process_error {
    std.fs::path executable = std.fs::path_from_utf8("/bin/echo");
    std.fs::path working_directory = std.fs::path_from_utf8("/");
    std.process::command command = std.process::command_create(&executable);
    std.process::arg(&command, "process-surface");
    std.process::environment(&command, "R_PROCESS_SURFACE", "configured");
    std.process::remove_environment(&command, "R_PROCESS_SURFACE");
    std.process::clear_environment(&command);
    std.process::working_directory(&command, &working_directory);
    drop command;
    drop working_directory;
    drop executable;
    return 0;
}

async i32 main() {
    try {
        if (configure_command() != 0) {
            return 1;
        }

        std.fs::path executable = std.fs::path_from_utf8("/bin/echo");
        std.process::command command = std.process::command_create(&executable);
        std.process::arg(&command, "async-process-surface");
        std.process::clear_environment(&command);
        o<std.time::instant> deadline = o::none;
        std.process::spawn_result outcome =
            await std.process::spawn(move command, deadline);
        drop outcome;
        drop executable;
        return 0;
    } catch (std.fs::path_error failure) {
        failure as void;
        return 2;
    } catch (std.process::process_error failure) {
        failure as void;
        return 3;
    } catch (std.async::start_error failure) {
        failure as void;
        return 4;
    }
}
