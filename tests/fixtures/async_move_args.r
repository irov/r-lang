module test.async_move_args;

error transfer_error {
    i32 code;
};

async void consume_array(array<u8> bytes) {
    return;
}

async i32 consume_paths(std.fs::path first, std.fs::path second) {
    return 7;
}

async array<u8> forward_array(array<u8> bytes, bool succeed) throws transfer_error {
    if (succeed == true) {
        return move bytes;
    }
    throw {.code = 9};
}

async i32 launch_array(array<u8> bytes) {
    try {
        task<void> operation = consume_array(move bytes);
        await move operation;
        return 0;
    } catch (std.async::start_error first_error) {
        first_error as void;
        try {
            task<void> retry_operation = consume_array(move bytes);
            await move retry_operation;
            return 1;
        } catch (std.async::start_error second_error) {
            second_error as void;
            return 2;
        }
    }
}

async i32 launch_paths(std.fs::path first, std.fs::path second) {
    try {
        task<i32> operation = consume_paths(move first, move second);
        i32 value = await move operation;
        return value;
    } catch (std.async::start_error error) {
        error as void;
        return 3;
    }
}

async i32 main() {
    return 0;
}

async void resume_effect_child() {
    return;
}

async i32 resume_effect_probe() {
    i32 counter = 0;
    try {
        task<void> operation = resume_effect_child();
        counter += 1;
        await move operation;
        return counter;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
}
