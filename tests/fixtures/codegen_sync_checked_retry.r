module test.codegen.sync_checked_retry;

error retry_error {
    i32 code;
};

error alternate_error {
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

protected void fail_void() throws retry_error {
    throw {
        .code = 7,
    };
}

protected void succeed_void() throws retry_error {
}

protected void fail_alternate() throws retry_error, alternate_error {
    throw alternate_error {
        .code = 31,
    };
}

protected void succeed_multi() throws alternate_error, retry_error {
}

protected void succeed_infallible() {
}

protected i32 succeed_value_infallible() {
    return 61;
}

protected i32 fail_value() throws retry_error {
    throw {
        .code = 9,
    };
}

protected i32 succeed_value() throws retry_error {
    return 42;
}

protected move_value fail_move_value() throws move_retry_error {
    own i32* payload = new i32(17);
    throw {
        .payload = move payload,
        .code = 17,
    };
}

protected move_value succeed_move_value() throws move_retry_error {
    own i32* payload = new i32(23);
    move_value value = {
        .payload = move payload,
        .code = 23,
    };
    return move value;
}

protected i32 verify_call_once() {
    std.sync::once state = std.sync::once_new();
    try {
        std.sync::call_once(&state, fail_void);
        return 1;
    } catch (retry_error error) {
        if (error.code != 7) {
            return 2;
        }
    }
    try {
        std.sync::call_once(&state, succeed_void);
    } catch (retry_error error) {
        error as void;
        return 3;
    }
    try {
        std.sync::call_once(&state, fail_void);
    } catch (retry_error error) {
        error as void;
        return 4;
    }
    return 0;
}

protected i32 verify_call_once_force() {
    std.sync::once state = std.sync::once_new();
    try {
        std.sync::call_once_force(&state, fail_void);
        return 1;
    } catch (retry_error error) {
        if (error.code != 7) {
            return 2;
        }
    }
    try {
        std.sync::call_once_force(&state, succeed_void);
    } catch (retry_error error) {
        error as void;
        return 3;
    }
    try {
        std.sync::call_once_force(&state, fail_void);
    } catch (retry_error error) {
        error as void;
        return 4;
    }
    return 0;
}

protected i32 verify_call_once_multiple_errors() {
    std.sync::once state = std.sync::once_new();
    try {
        std.sync::call_once(&state, fail_alternate);
        return 1;
    } catch (retry_error error) {
        error as void;
        return 2;
    } catch (alternate_error error) {
        if (error.code != 31) {
            return 3;
        }
    }
    try {
        std.sync::call_once(&state, succeed_multi);
    } catch (retry_error error) {
        error as void;
        return 4;
    } catch (alternate_error error) {
        error as void;
        return 5;
    }
    return 0;
}

protected i32 verify_infallible_initializers() {
    std.sync::once once_state = std.sync::once_new();
    std.sync::call_once(&once_state, succeed_infallible);

    std.sync::once_lock<i32> lock_state = std.sync::once_lock::<i32>();
    const i32* value = std.sync::get_or_init(&lock_state, succeed_value_infallible);
    if (*value != 61) {
        return 1;
    }
    return 0;
}

protected i32 verify_get_or_init() {
    std.sync::once_lock<i32> state = std.sync::once_lock::<i32>();
    try {
        const i32* failed = std.sync::get_or_init(&state, fail_value);
        failed as void;
        return 1;
    } catch (retry_error error) {
        if (error.code != 9) {
            return 2;
        }
    }

    try {
        const i32* value = std.sync::get_or_init(&state, succeed_value);
        if (*value != 42) {
            return 3;
        }
    } catch (retry_error error) {
        error as void;
        return 4;
    }
    try {
        const i32* existing = std.sync::get_or_init(&state, fail_value);
        if (*existing != 42) {
            return 5;
        }
    } catch (retry_error error) {
        error as void;
        return 6;
    }
    return 0;
}

protected i32 verify_move_get_or_init() {
    std.sync::once_lock<move_value> state = std.sync::once_lock::<move_value>();
    try {
        const move_value* failed = std.sync::get_or_init(&state, fail_move_value);
        failed as void;
        return 1;
    } catch (move_retry_error error) {
        if (error.code != 17) {
            return 2;
        }
    }
    try {
        const move_value* value = std.sync::get_or_init(&state, succeed_move_value);
        if (value->code != 23) {
            return 3;
        }
    } catch (move_retry_error error) {
        drop error;
        return 4;
    }
    try {
        const move_value* existing = std.sync::get_or_init(&state, fail_move_value);
        if (existing->code != 23) {
            return 5;
        }
    } catch (move_retry_error error) {
        drop error;
        return 6;
    }
    return 0;
}

i32 main() {
    i32 once_status = verify_call_once();
    if (once_status != 0) {
        return 10 + once_status;
    }
    i32 force_status = verify_call_once_force();
    if (force_status != 0) {
        return 20 + force_status;
    }
    i32 multi_status = verify_call_once_multiple_errors();
    if (multi_status != 0) {
        return 30 + multi_status;
    }
    i32 infallible_status = verify_infallible_initializers();
    if (infallible_status != 0) {
        return 40 + infallible_status;
    }
    i32 lock_status = verify_get_or_init();
    if (lock_status != 0) {
        return 50 + lock_status;
    }
    i32 move_lock_status = verify_move_get_or_init();
    if (move_lock_status != 0) {
        return 60 + move_lock_status;
    }
    return 0;
}
