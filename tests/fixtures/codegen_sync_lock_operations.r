module test.codegen.sync_lock_operations;

i32 main() {
    std.sync::mutex<i32> mutex = std.sync::mutex_new(10);
    std.sync::lock_result<i32> acquired = std.sync::lock(&mutex);
    switch (move acquired) {
        case variant std.sync::lock_result::locked(move guard):
            {
                i32* value = std.sync::mutex_guard_mut(&guard);
                if (*value != 10) { return 1; }
                *value = 11;
            }
            std.sync::try_lock_result<i32> recursive = std.sync::try_lock(&mutex);
            switch (move recursive) {
                case variant std.sync::try_lock_result::locked(move nested):
                    std.sync::unlock(move nested); return 2;
                case variant std.sync::try_lock_result::poisoned(move nested):
                    std.sync::unlock(move nested); return 3;
                case variant std.sync::try_lock_result::would_deadlock:
                    break;
                case variant std.sync::try_lock_result::would_block:
                    return 4;
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 5;
        case variant std.sync::lock_result::would_deadlock:
            return 6;
    }
    std.sync::try_lock_result<i32> reacquired = std.sync::try_lock(&mutex);
    switch (move reacquired) {
        case variant std.sync::try_lock_result::locked(move guard):
            {
                const i32* value = std.sync::mutex_guard_ref(&guard);
                if (*value != 11) { return 7; }
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::try_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 8;
        case variant std.sync::try_lock_result::would_deadlock:
            return 9;
        case variant std.sync::try_lock_result::would_block:
            return 10;
    }

    std.sync::rw_lock<i32> lock = std.sync::rwlock_new(20);
    std.sync::read_lock_result<i32> read = std.sync::read(&lock);
    switch (move read) {
        case variant std.sync::read_lock_result::locked(move guard):
            {
                const i32* value = std.sync::rw_read_guard_ref(&guard);
                if (*value != 20) { return 11; }
            }
            std.sync::try_write_lock_result<i32> conflict = std.sync::try_write(&lock);
            switch (move conflict) {
                case variant std.sync::try_write_lock_result::locked(move nested):
                    std.sync::unlock(move nested); return 12;
                case variant std.sync::try_write_lock_result::poisoned(move nested):
                    std.sync::unlock(move nested); return 13;
                case variant std.sync::try_write_lock_result::would_deadlock:
                    break;
                case variant std.sync::try_write_lock_result::would_block:
                    return 14;
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::read_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 15;
        case variant std.sync::read_lock_result::would_deadlock:
            return 16;
    }
    std.sync::write_lock_result<i32> write = std.sync::write(&lock);
    switch (move write) {
        case variant std.sync::write_lock_result::locked(move guard):
            {
                i32* value = std.sync::rw_write_guard_mut(&guard);
                *value = 21;
                const i32* observed = std.sync::rw_write_guard_ref(&guard);
                if (*observed != 21) { return 17; }
            }
            std.sync::try_read_lock_result<i32> conflict = std.sync::try_read(&lock);
            switch (move conflict) {
                case variant std.sync::try_read_lock_result::locked(move nested):
                    std.sync::unlock(move nested); return 18;
                case variant std.sync::try_read_lock_result::poisoned(move nested):
                    std.sync::unlock(move nested); return 19;
                case variant std.sync::try_read_lock_result::would_deadlock:
                    break;
                case variant std.sync::try_read_lock_result::would_block:
                    return 20;
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::write_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 21;
        case variant std.sync::write_lock_result::would_deadlock:
            return 22;
    }
    std.sync::try_read_lock_result<i32> final_read = std.sync::try_read(&lock);
    switch (move final_read) {
        case variant std.sync::try_read_lock_result::locked(move guard):
            {
                const i32* value = std.sync::rw_read_guard_ref(&guard);
                if (*value != 21) { return 23; }
            }
            std.sync::unlock(move guard);
            break;
        case variant std.sync::try_read_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 24;
        case variant std.sync::try_read_lock_result::would_deadlock:
            return 25;
        case variant std.sync::try_read_lock_result::would_block:
            return 26;
    }
    std.sync::try_write_lock_result<i32> final_write = std.sync::try_write(&lock);
    switch (move final_write) {
        case variant std.sync::try_write_lock_result::locked(move guard):
            std.sync::unlock(move guard);
            break;
        case variant std.sync::try_write_lock_result::poisoned(move guard):
            std.sync::unlock(move guard); return 27;
        case variant std.sync::try_write_lock_result::would_deadlock:
            return 28;
        case variant std.sync::try_write_lock_result::would_block:
            return 29;
    }
    return 0;
}
