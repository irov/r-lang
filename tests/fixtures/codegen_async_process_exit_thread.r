module test.codegen.async_process_exit_thread;

import std.process;

/* R-SLIB-PROC-0007: exit from a scoped thread while the task that joins it waits; the joining
   task can never resume, so the shutdown does not wait for it. */

void stop(i32 code) {
    std.process::exit(code);
}

async void pause() {}

async i32 main() {
    try {
        await pause();
        thread_scope {
            std.thread::scoped_join_handle<void> handle = std.thread::spawn_scoped(stop, 13);
            std.thread::join_result<void> joined = std.thread::join(move handle);
            drop joined;
        }
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 90;
    } catch (std.async::start_error failure) {
        failure as void;
        return 91;
    }
    return 1;
}
