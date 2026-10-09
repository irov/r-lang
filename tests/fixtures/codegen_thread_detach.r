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

i32 worker(i32 code) throws worker_error {
    throw (code != 0) worker_error {.code = code};
    return code;
}

/* R-LIB-0010: running checked threads are detached through a handle moved into a synchronous and
   an async function. The wrapper refuses the first async start; R-SLIB-ASYNC-0003 leaves the
   handle with main, which must still resolve it (R-LIB-0010) and detaches it itself. */
async i32 main() {
    try {
        std.thread::join_handle<i32 throws worker_error> returning = std.thread::spawn(worker, 0);
        detach_worker(move returning);
        std.thread::join_handle<i32 throws worker_error> refused = std.thread::spawn(worker, 5);
        try {
            await z_detach_async(move refused);
            return 1;
        } catch (std.async::start_error failure) {
            std.thread::detach(move refused);
        }
        std.thread::join_handle<i32 throws worker_error> throwing = std.thread::spawn(worker, 6);
        try {
            await z_detach_async(move throwing);
        } catch (std.async::start_error failure) {
            std.thread::detach(move throwing);
            return 2;
        }
        return 0;
    } catch (std.thread::thread_error failure) {
        return 3;
    }
}
