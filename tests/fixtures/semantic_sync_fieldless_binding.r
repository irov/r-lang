module test.semantic.sync_fieldless_binding;

i32 main() {
    std.sync::once_lock<i32> once = std.sync::once_lock::<i32>();
    std.sync::set_result<i32> value = std.sync::set(&once, 1);
    switch (move value) {
        case variant std.sync::set_result::stored(move bad): return bad;
        case variant std.sync::set_result::occupied(move previous): return previous;
    }
}
