module test.codegen.async_process_exit;

import std.process;

/* R-SLIB-PROC-0007: std.process::exit called by a task on an executor worker ends the process
   with the requested status after the orderly shutdown; the awaiting root task never resumes. */

async void pause() {}

async void work(i32 code) throws std.async::start_error {
    await pause();
    std.process::exit(code);
}

async i32 main() {
    try {
        await work(7);
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
    return 1;
}
