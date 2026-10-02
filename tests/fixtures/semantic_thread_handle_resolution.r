module test.semantic.thread_handle_resolution;

error worker_error {
    i32 code;
};

void join_unscoped(std.thread::join_handle<i32 throws worker_error> handle)
    throws worker_error {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}

void detach_unscoped(std.thread::join_handle<i32 throws worker_error> handle) {
    std.thread::detach(move handle);
}

void join_scoped(std.thread::scoped_join_handle<i32 throws worker_error> handle)
    throws worker_error {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}
