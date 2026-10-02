module test.codegen.async_process_exit_scope;

import std.process;

/* R-SLIB-PROC-0007: exit from a scoped child cancels its sibling, which is suspended on a long
   timer, and drains it before the process ends; the parent frame that both borrow stays
   allocated because it never resumes. */

async void pause() {}

@scoped
async u32 sleeper(i32* cleanups) throws std.async::start_error, std.time::time_error {
    try {
        await std.time::sleep_for(std.time::duration_from_seconds(600i64));
    } finally {
        *cleanups += 1;
    }
    return 7u32;
}

@scoped
async u32 quitter(const i32* code) throws std.async::start_error {
    await pause();
    std.process::exit(*code);
}

async i32 main() {
    i32 cleanups = 0;
    i32 code = 11;
    try {
        task_scope(2) group {
            auto slow = sleeper(&cleanups);
            auto fast = quitter(&code);
            await group.all();
            u32 first = await move slow;
            u32 second = await move fast;
            first as void;
            second as void;
        }
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    } catch (std.time::time_error failure) {
        failure as void;
        return 91;
    }
    return 1;
}
