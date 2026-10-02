module regression.async_sync_guard_across_await;

async void pause() {}

async i32 main() {
    std.sync::mutex<i32> mutex = std.sync::mutex_new(1);
    std.sync::lock_result<i32> locked = std.sync::lock(&mutex);
    switch (move locked) {
        case variant std.sync::lock_result::locked(move guard):
            try {
                task<void> operation = pause();
                await move operation;
            } catch (std.async::start_error error) {
                error as void;
                std.sync::unlock(move guard);
                return 2;
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::lock_result::poisoned(move guard):
            std.sync::unlock(move guard); break;
        case variant std.sync::lock_result::would_deadlock: return 1;
    }
    return 0;
}
