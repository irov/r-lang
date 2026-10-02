module audit.std_sync_simple;

i32 sync_probe() throws std.sync::barrier_error {
    std.sync::barrier barrier = std.sync::barrier_new(1usize);
    std.sync::barrier_wait_result waited = std.sync::barrier_wait(&barrier);
    waited as void;

    std.sync::condvar condition = std.sync::condvar_new();
    std.sync::notify_one(&condition);
    std.sync::notify_all(&condition);
    drop condition;
    drop barrier;
    return 0;
}

i32 zero_count_probe() {
    try {
        std.sync::barrier invalid = std.sync::barrier_new(0usize);
        drop invalid;
        return 1;
    } catch (std.sync::barrier_error failure) {
        failure as void;
        return 0;
    }
}

async i32 main() {
    try {
        if (sync_probe() != 0) {
            return 1;
        }
        if (zero_count_probe() != 0) {
            return 2;
        }

        std.sync::barrier barrier = std.sync::barrier_new(1usize);
        std.sync::barrier_wait_result waited = std.sync::barrier_wait(&barrier);
        waited as void;
        std.sync::condvar condition = std.sync::condvar_new();
        std.sync::notify_one(&condition);
        std.sync::notify_all(&condition);
        drop condition;
        drop barrier;
        return 0;
    } catch (std.sync::barrier_error failure) {
        failure as void;
        return 4;
    }
}
