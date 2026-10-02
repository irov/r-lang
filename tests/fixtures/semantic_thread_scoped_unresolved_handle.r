module test.semantic.thread_scoped_unresolved_handle;

error worker_error {
    i32 code;
};

void leave_unresolved(std.thread::scoped_join_handle<i32 throws worker_error> handle) {
}
