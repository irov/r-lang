module test.codegen.thread_join;

error worker_error {
    i32 code;
};

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

@scoped
async void join_async_fallible(
    std.thread::scoped_join_handle<i32 throws worker_error> handle) throws worker_error {
    std.thread::join_result<i32> completed = std.thread::join(move handle);
    drop completed;
}

i32 main() {
    return 0;
}
