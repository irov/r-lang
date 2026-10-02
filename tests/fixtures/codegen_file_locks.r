module test.codegen.file_locks;

/* R-SLIB-FS-0016: advisory locks of two open files of one path: try_lock and its conflict, a
   lock that times out in a deadline block, a lock that waits until another task unlocks, shared
   locks that coexist and the access a lock needs; the path is the first program argument. */

protected std.fs::open_file_options settings(std.fs::access access) {
    return std.fs::open_file_options { .access = access, .create = std.fs::create_mode::open_or_create,
        .truncate = false, .append = false, .follow_final_symlink = false };
}

protected std.time::instant after_milliseconds(u32 ms) throws std.time::time_error, std.time::duration_error {
    std.time::instant now = std.time::monotonic_now();
    return now.add(std.time::duration_from_parts(0i64, ms * 1000000u32));
}

/* Unlocks and closes holder after ms milliseconds. */
protected async void release_later(std.fs::file holder, u32 ms) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, ms * 1000000u32));
    await holder.unlock(0u64, 0u64);
    await (move holder).close();
}

async i32 main(const str[] arguments) {
    try {
        if (len(arguments) != 2usize) { throw TestAssertionFailed {.code = 10}; }
        try {
            std.fs::path path = std.fs::path_from_utf8(arguments[1]);
            std.fs::file first = await path.open_file(settings(std.fs::access::read_write));
            std.fs::file second = await path.open_file(settings(std.fs::access::read_write));
            if (await first.try_lock(std.fs::lock_kind::exclusive, 0u64, 0u64) == false) { throw TestAssertionFailed {.code = 11}; }
            if (await std.fs::try_lock(&second, std.fs::lock_kind::shared, 4u64, 4u64) == true) { throw TestAssertionFailed {.code = 12}; }
            bool timed_out = false;
            try {
                deadline (after_milliseconds(40u32)) {
                    await second.lock(std.fs::lock_kind::shared, 0u64, 16u64);
                }
            } catch (std.fs::fs_error failure) {
                timed_out = failure.code == std.fs::error_code::timed_out;
            }
            if (timed_out == false) { throw TestAssertionFailed {.code = 13}; }
            task_scope(2) group {
                auto releasing = release_later(move first, 30u32);
                await std.fs::lock(&second, std.fs::lock_kind::exclusive, 0u64, 0u64);
                await move releasing;
            }
            std.fs::file reader = await path.open_file(settings(std.fs::access::read));
            if (await reader.try_lock(std.fs::lock_kind::shared, 0u64, 0u64) == true) { throw TestAssertionFailed {.code = 14}; }
            await std.fs::unlock(&second, 0u64, 0u64);
            if (await reader.try_lock(std.fs::lock_kind::shared, 0u64, 0u64) == false) { throw TestAssertionFailed {.code = 15}; }
            if (await second.try_lock(std.fs::lock_kind::shared, 0u64, 0u64) == false) { throw TestAssertionFailed {.code = 16}; }
            bool needs_write = false;
            try {
                bool taken = await reader.try_lock(std.fs::lock_kind::exclusive, 0u64, 0u64);
                taken as void;
            } catch (std.fs::fs_error failure) {
                needs_write = failure.code == std.fs::error_code::invalid_operation;
            }
            if (needs_write == false) { throw TestAssertionFailed {.code = 17}; }
            await (move reader).close();
            await (move second).close();
            return 0;
        } catch (std.fs::path_error failure) { throw TestAssertionFailed {.code = 65}; }
        catch (std.error::fault failure) { throw TestAssertionFailed {.code = 66}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
