module regression.sync_guard_borrow_after_unlock;

i32 main() {
    std.sync::mutex<i32> mutex = std.sync::mutex_new(1);
    std.sync::lock_result<i32> locked = std.sync::lock(&mutex);
    switch (move locked) {
        case variant std.sync::lock_result::locked(move guard):
            i32* value = std.sync::mutex_guard_mut(&guard);
            std.sync::unlock(move guard);
            *value = 2;
            break;
        case variant std.sync::lock_result::poisoned(move guard):
            std.sync::unlock(move guard); break;
        case variant std.sync::lock_result::would_deadlock: return 1;
    }
    return 0;
}
