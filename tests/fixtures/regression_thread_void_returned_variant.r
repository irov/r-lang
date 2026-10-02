module test.thread_void_returned_variant;
void inspect(std.thread::join_result<void> value) {
    switch (move value) {
    case variant std.thread::join_result::returned(move result): break;
    case variant std.thread::join_result::panicked(move report): break;
    }
}
