module test.codegen.library_task_id_outside;

// R-SLIB-ASYNC-0018 (M23): outside every task, in a synchronous main and on a thread of
// std.thread, the identifier is zero; the read allocates nothing and never waits.

@noalloc @nonblocking
u64 current() {
    return std.async::task_id();
}

u64 worker(u64 unused) {
    unused as void;
    return current();
}

i32 main() {
    i32 status = 0;
    if (current() != 0u64) { status += 1; }
    try {
        std.thread::join_handle<u64> handle = std.thread::spawn(worker, 7u64);
        std.thread::join_result<u64> result = std.thread::join(move handle);
        switch (move result) {
        case variant std.thread::join_result::returned(move value):
            if (value != 0u64) { status += 2; }
        case variant std.thread::join_result::panicked(move report):
            drop report;
            status += 4;
        }
    } catch (std.thread::thread_error failure) {
        failure as void;
        status += 8;
    }
    return status;
}
