module test.semantic.thread_spawn;

error worker_error {
    i32 code;
};

i32 worker(i32 value, array<u8> payload) throws worker_error {
    drop payload;
    return value;
}

void exercise(array<u8> payload) throws std.thread::thread_error {
    std.thread::join_handle<i32 throws worker_error> handle =
        std.thread::spawn(worker, 7, move payload);
    std.thread::detach(move handle);
}

void scoped_exercise(array<u8> payload)
    throws std.thread::thread_error, worker_error {
    thread_scope {
        std.thread::scoped_join_handle<i32 throws worker_error> handle =
            std.thread::spawn_scoped(worker, 9, move payload);
        std.thread::join_result<i32> outcome = std.thread::join(move handle);
        drop outcome;
    }
}

void start_failure_preserves_move(array<u8> payload) {
    try {
        std.thread::join_handle<i32 throws worker_error> handle =
            std.thread::spawn(worker, 11, move payload);
        std.thread::detach(move handle);
    } catch (std.thread::thread_error error) {
        drop payload;
        error as void;
    }
}
