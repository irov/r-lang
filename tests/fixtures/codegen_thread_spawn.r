module test.codegen.thread_spawn;

error worker_error {
    i32 code;
    array<u8> payload;
};

i32 success_worker(i32 value, array<u8> payload) {
    drop payload;
    return value;
}

i32 error_worker(i32 code, array<u8> payload) throws worker_error {
    throw {.code = code, .payload = move payload};
}

void start_failure_preserves_move(array<u8> payload) {
    try {
        std.thread::join_handle<i32> handle =
            std.thread::spawn(success_worker, 11, move payload);
        std.thread::detach(move handle);
    } catch (std.thread::thread_error error) {
        drop payload;
        error as void;
    }
}

void spawn_and_join(array<u8> payload)
    throws std.thread::thread_error, worker_error {
    std.thread::join_handle<i32> handle =
        std.thread::spawn(success_worker, 41, move payload);
    std.thread::join_result<i32> result = std.thread::join(move handle);
    drop result;
}

void spawn_error_and_join(array<u8> payload)
    throws std.thread::thread_error, worker_error {
    std.thread::join_handle<i32 throws worker_error> handle =
        std.thread::spawn(error_worker, -73, move payload);
    std.thread::join_result<i32> result = std.thread::join(move handle);
    drop result;
}

void spawn_scoped_and_join(array<u8> payload)
    throws std.thread::thread_error, worker_error {
    thread_scope {
        std.thread::scoped_join_handle<i32> handle =
            std.thread::spawn_scoped(success_worker, 42, move payload);
        std.thread::join_result<i32> result = std.thread::join(move handle);
        drop result;
    }
}

void spawn_error_and_detach(array<u8> payload) throws std.thread::thread_error {
    std.thread::join_handle<i32 throws worker_error> handle =
        std.thread::spawn(error_worker, -91, move payload);
    std.thread::detach(move handle);
}

async void spawn_async(array<u8> payload) throws std.thread::thread_error {
    std.thread::join_handle<i32> handle =
        std.thread::spawn(success_worker, 43, move payload);
    std.thread::detach(move handle);
}

/* R-LIB-0005: each payload is a one-byte array marked with the value the wrapper looks for in
   its drops. The wrapper refuses the first thread creation, so the first payload comes back to
   start_failure_preserves_move; the last two threads are detached and finish before the hosted
   drain ends the program. */
async i32 main() {
    try {
        array<u8> refused = std.alloc::bytes(1usize, 11u8);
        start_failure_preserves_move(move refused);
        array<u8> joined = std.alloc::bytes(1usize, 41u8);
        spawn_and_join(move joined);
        array<u8> failing = std.alloc::bytes(1usize, 73u8);
        try {
            spawn_error_and_join(move failing);
            return 1;
        } catch (worker_error error) {
            if (error.code != -73) { return 2; }
        }
        array<u8> scoped = std.alloc::bytes(1usize, 42u8);
        spawn_scoped_and_join(move scoped);
        array<u8> detached = std.alloc::bytes(1usize, 91u8);
        spawn_error_and_detach(move detached);
        array<u8> started = std.alloc::bytes(1usize, 43u8);
        await spawn_async(move started);
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 3;
    } catch (std.thread::thread_error error) {
        error as void;
        return 4;
    } catch (worker_error error) {
        return 5;
    } catch (std.async::start_error error) {
        error as void;
        return 6;
    }
}
