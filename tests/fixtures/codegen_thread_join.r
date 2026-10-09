module test.codegen.thread_join;

error worker_error {
    i32 code;
};

i32 plain_worker() {
    return 41;
}

i32 checked_worker(bool fail) throws worker_error {
    throw (fail == true) worker_error {.code = 73};
    return 42;
}

void join_infallible(std.thread::join_handle<i32> handle) {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}

void join_fallible(std.thread::join_handle<i32 throws worker_error> handle)
    throws worker_error {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}

/* R-BORROW-0023, R-BORROW-0024: a scoped handle carries its thread region, so only a @scoped
   async frame takes one. */
@scoped
async void join_async_infallible(std.thread::scoped_join_handle<i32> handle) {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}

/* Only analyzed: R-MEM-0016 requires the scope that spawns a fallible scoped handle to join it
   itself, so no caller can move one into this function. */
@scoped
async void join_async_fallible(
    std.thread::scoped_join_handle<i32 throws worker_error> handle) throws worker_error {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}

/* R-BORROW-0024: the scoped handle stays live across the await, so the thread scope belongs to
   a @scoped frame. */
@scoped
async void join_scoped() throws std.thread::thread_error, std.async::start_error {
    thread_scope {
        std.thread::scoped_join_handle<i32> scoped_plain =
            std.thread::spawn_scoped(plain_worker);
        task_scope(1) group {
            await join_async_infallible(move scoped_plain);
        }
    }
}

/* Each join form runs on a real thread: the program checks that a thrown worker error reaches
   it with its code, and the wrapper checks the values the joins move out. */
async i32 main() {
    try {
        std.thread::join_handle<i32> plain = std.thread::spawn(plain_worker);
        join_infallible(move plain);
        std.thread::join_handle<i32 throws worker_error> succeeding =
            std.thread::spawn(checked_worker, false);
        join_fallible(move succeeding);
        i32 observed = 0;
        try {
            std.thread::join_handle<i32 throws worker_error> failing =
                std.thread::spawn(checked_worker, true);
            join_fallible(move failing);
            return 1;
        } catch (worker_error failure) {
            observed = failure.code;
        }
        if (observed != 73) {
            return 2;
        }
        task_scope(1) group {
            await join_scoped();
        }
        return 0;
    } catch (std.thread::thread_error failure) {
        return 5;
    } catch (std.async::start_error failure) {
        return 6;
    } catch (worker_error failure) {
        return 7;
    }
}
