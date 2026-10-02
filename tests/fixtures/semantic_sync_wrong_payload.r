module test.semantic.sync_wrong_payload;

i32 main() {
    std.sync::once_lock<i32> once = std.sync::once_lock::<i32>();
    std.sync::set_result<i32> value = std.sync::set(&once, true);
    drop value;
    return 0;
}
