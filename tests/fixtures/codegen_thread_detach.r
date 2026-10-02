module test.codegen.thread_detach;

error worker_error {
    i32 code;
};

void detach_worker(std.thread::join_handle<i32 throws worker_error> handle) {
    std.thread::detach(move handle);
}

async void z_detach_async(std.thread::join_handle<i32 throws worker_error> handle) {
    std.thread::detach(move handle);
}

i32 main() {
    return 0;
}
