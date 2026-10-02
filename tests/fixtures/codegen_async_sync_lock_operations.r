module test.codegen.async_sync_lock_operations;

async i32 main() {
    std.sync::mutex<i32> mutex = std.sync::mutex_new(31);
    std.sync::lock_result<i32> locked = std.sync::lock(&mutex);
    switch (move locked) {
        case variant std.sync::lock_result::locked(move guard):
            {
                i32* value = std.sync::mutex_guard_mut(&guard);
                *value = 32;
                const i32* observed = std.sync::mutex_guard_ref(&guard);
                if (*observed != 32) { return 1; }
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 2;
        case variant std.sync::lock_result::would_deadlock:
            return 3;
    }
    std.sync::try_lock_result<i32> tried = std.sync::try_lock(&mutex);
    switch (move tried) {
        case variant std.sync::try_lock_result::locked(move guard):
            std.sync::unlock(move guard);
            break;
        case variant std.sync::try_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 4;
        case variant std.sync::try_lock_result::would_deadlock:
            return 5;
        case variant std.sync::try_lock_result::would_block:
            return 6;
    }

    std.sync::rw_lock<i32> lock = std.sync::rwlock_new(41);
    std.sync::read_lock_result<i32> read = std.sync::read(&lock);
    switch (move read) {
        case variant std.sync::read_lock_result::locked(move guard):
            {
                const i32* value = std.sync::rw_read_guard_ref(&guard);
                if (*value != 41) { return 7; }
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::read_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 8;
        case variant std.sync::read_lock_result::would_deadlock:
            return 9;
    }
    std.sync::try_read_lock_result<i32> tried_read = std.sync::try_read(&lock);
    switch (move tried_read) {
        case variant std.sync::try_read_lock_result::locked(move guard):
            std.sync::unlock(move guard);
            break;
        case variant std.sync::try_read_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 10;
        case variant std.sync::try_read_lock_result::would_deadlock:
            return 11;
        case variant std.sync::try_read_lock_result::would_block:
            return 12;
    }
    std.sync::write_lock_result<i32> write = std.sync::write(&lock);
    switch (move write) {
        case variant std.sync::write_lock_result::locked(move guard):
            {
                i32* value = std.sync::rw_write_guard_mut(&guard);
                *value = 42;
                const i32* observed = std.sync::rw_write_guard_ref(&guard);
                if (*observed != 42) { return 13; }
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::write_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 14;
        case variant std.sync::write_lock_result::would_deadlock:
            return 15;
    }
    std.sync::try_write_lock_result<i32> tried_write = std.sync::try_write(&lock);
    switch (move tried_write) {
        case variant std.sync::try_write_lock_result::locked(move guard):
            std.sync::unlock(move guard);
            break;
        case variant std.sync::try_write_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 16;
        case variant std.sync::try_write_lock_result::would_deadlock:
            return 17;
        case variant std.sync::try_write_lock_result::would_block:
            return 18;
    }
    return 0;
}
