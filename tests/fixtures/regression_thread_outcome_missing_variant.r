module test.thread_outcome_missing_variant;
i32 inspect(std.thread::join_result<i32> value) {
    switch (move value) {
    case variant std.thread::join_result::returned(move result): return result;
    }
}
