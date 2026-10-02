module test.codegen.async_sync_checked_retry;

error retry_error {
    i32 code;
};

struct move_value {
    own i32* payload;
    i32 code;
};

error move_retry_error {
    own i32* payload;
    i32 code;
};

protected void fail_once() throws retry_error {
    throw {
        .code = 7,
    };
}

protected void succeed_once() throws retry_error {
}

protected move_value fail_value() throws move_retry_error {
    own i32* payload = new i32(17);
    throw {
        .payload = move payload,
        .code = 17,
    };
}

protected move_value succeed_value() throws move_retry_error {
    own i32* payload = new i32(23);
    move_value value = {
        .payload = move payload,
        .code = 23,
    };
    return move value;
}

async i32 main() {
    std.sync::once once_state = std.sync::once_new();
    try {
        std.sync::call_once(&once_state, fail_once);
        return 1;
    } catch (retry_error error) {
        if (error.code != 7) {
            return 2;
        }
    }
    try {
        std.sync::call_once(&once_state, succeed_once);
    } catch (retry_error error) {
        error as void;
        return 3;
    }

    std.sync::once_lock<move_value> lock_state = std.sync::once_lock::<move_value>();
    try {
        const move_value* failed = std.sync::get_or_init(&lock_state, fail_value);
        failed as void;
        return 4;
    } catch (move_retry_error error) {
        if (error.code != 17) {
            drop error;
            return 5;
        }
    }
    try {
        const move_value* value = std.sync::get_or_init(&lock_state, succeed_value);
        if (value->code != 23) {
            return 6;
        }
    } catch (move_retry_error error) {
        drop error;
        return 7;
    }
    try {
        const move_value* existing = std.sync::get_or_init(&lock_state, fail_value);
        if (existing->code != 23) {
            return 8;
        }
    } catch (move_retry_error error) {
        drop error;
        return 9;
    }
    return 0;
}
