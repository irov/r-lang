module test.semantic.thread_scoped_detach;

error worker_error {
    i32 code;
};

void invalid_detach(std.thread::scoped_join_handle<i32 throws worker_error> handle) {
    std.thread::detach(move handle);
}
