module test.semantic.sync_borrow_drop;

i32 main() {
    std.sync::once_lock<i32> once = std.sync::once_lock::<i32>();
    o<const i32*> value = std.sync::get(&once);
    drop once;
    switch (value) {
        case variant o::none: return 0;
        case variant o::some(pointer): return **pointer;
    }
}
