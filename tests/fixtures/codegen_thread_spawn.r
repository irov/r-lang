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

i32 main() {
    return 0;
}
