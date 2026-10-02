module test.thread_outcome_non_send;
async void inspect(std.thread::join_result<rc i32> value) {
    drop value;
}
