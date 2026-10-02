module test.semantic.sync_wrong_result;

i32 main() {
    std.sync::once_lock<i32> once = std.sync::once_lock::<i32>();
    std.sync::recv_result<i32> value = std.sync::set(&once, 1);
    drop value;
    return 0;
}
