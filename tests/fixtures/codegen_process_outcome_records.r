module test.process_outcome_records;

std.process::child take_child(std.process::spawn_result result) throws std.process::process_error {
    switch (move result) {
    case variant std.process::spawn_result::spawned(move child): return move child;
    case variant std.process::spawn_result::failed(move failure): throw failure.error;
    }
}

std.process::exit_status take_status(std.process::wait_result result) throws std.process::process_error {
    switch (move result) {
    case variant std.process::wait_result::exited(move status): return status;
    case variant std.process::wait_result::failed(move failure): throw failure.error;
    }
}

async i32 main() {
    try {
        try {
            std.fs::path path = std.fs::path_from_utf8("/usr/bin/true");
            std.process::command command = std.process::command_create(&path);
            std.process::stdio streams = { .input = std.process::pipe_mode::null_device,
                .output = std.process::pipe_mode::null_device, .error = std.process::pipe_mode::null_device };
            std.process::set_stdio(&command, streams);
            std.process::spawn_result started = await std.process::spawn(move command, o::none);
            std.process::child child = take_child(move started);
            std.process::wait_result finished = await std.process::wait(move child, o::none);
            std.process::exit_status status = take_status(move finished);
            if ((status.kind != std.process::termination_kind::exited) || (status.code != 0) ||
                (status.success != true)) { throw TestAssertionFailed {.code = 1}; }
            std.fs::path missing = std.fs::path_from_utf8("/r-example-missing-process-outcome-records");
            std.process::command invalid = std.process::command_create(&missing);
            std.process::spawn_result failed = await std.process::spawn(move invalid, o::none);
            try {
                std.process::child unexpected = take_child(move failed);
                throw TestAssertionFailed {.code = 2};
            } catch (std.process::process_error failure) {
                if (failure.code != std.process::error_code::not_found) { throw TestAssertionFailed {.code = 3}; }
            }
            return 0;
        } catch (std.process::process_error failure) { throw TestAssertionFailed {.code = 4}; }
        catch (std.fs::path_error failure) { throw TestAssertionFailed {.code = 5}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 6}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
