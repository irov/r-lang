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

/* The wrapper holds this sleep until the probe has registered its await, so the probe always
   suspends there. */
async void resume_effect_child() {
    std.thread::sleep_nanoseconds(0u64);
    return;
}

/* The increment and the yield run between the start and the await; a resumed step that
   repeated them would return 2 and yield twice. */
async i32 resume_effect_probe() {
    i32 counter = 0;
    try {
        task<void> operation = resume_effect_child();
        counter += 1;
        std.thread::yield_now();
        await move operation;
        return counter;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
}

bool holds(const array<u8>* bytes, u8 marker) {
    return len(*bytes) == 1usize && (*bytes)[0] == marker;
}

bool names(const std.fs::path* path, str expected) throws std.fs::path_error {
    std.string::string text = std.fs::path_to_utf8(path);
    return std.bytes::equal(text, expected);
}

/* R-SLIB-ASYNC-0002, R-SLIB-ASYNC-0003: async starts with named Move arguments, through named
   task values. The wrapper refuses chosen starts (see its table); every refused start shall
   leave its arguments with the caller, and every argument shall be dropped exactly once. */
async i32 main() {
    try {
        i32 resumed = await resume_effect_probe();
        if (resumed != 1) {
            return 10;
        }

        array<u8> refused = std.alloc::bytes(1usize, 17u8);
        try {
            task<void> operation = consume_array(move refused);
            await move operation;
            return 11;
        } catch (std.async::start_error error) {
            if (error != std.async::start_error::allocation_failed) {
                return 12;
            }
        }
        if (holds(&refused, 17u8) == false) {
            return 13;
        }
        task<void> accepted = consume_array(move refused);
        await move accepted;

        array<u8> retried = std.alloc::bytes(1usize, 23u8);
        task<i32> launched = launch_array(move retried);
        i32 attempts = await move launched;
        if (attempts != 1) {
            return 14;
        }

        std.fs::path first = std.fs::path_from_utf8("first");
        std.fs::path second = std.fs::path_from_utf8("second");
        try {
            task<i32> operation = consume_paths(move first, move second);
            i32 unexpected = await move operation;
            return 15 + unexpected;
        } catch (std.async::start_error error) {
            if (error != std.async::start_error::allocation_failed) {
                return 16;
            }
        }
        if (names(&first, "first") == false || names(&second, "second") == false) {
            return 17;
        }
        task<i32> paths = consume_paths(move first, move second);
        i32 consumed = await move paths;
        if (consumed != 7) {
            return 18;
        }
        std.fs::path third = std.fs::path_from_utf8("third");
        std.fs::path fourth = std.fs::path_from_utf8("fourth");
        task<i32> launched_paths = launch_paths(move third, move fourth);
        i32 forwarded_paths = await move launched_paths;
        if (forwarded_paths != 7) {
            return 19;
        }

        array<u8> forwarded = std.alloc::bytes(1usize, 41u8);
        task<array<u8> throws transfer_error> forwarding = forward_array(move forwarded, true);
        array<u8> returned = await move forwarding;
        if (holds(&returned, 41u8) == false) {
            return 20;
        }
        drop returned;
        array<u8> thrown = std.alloc::bytes(1usize, 43u8);
        try {
            task<array<u8> throws transfer_error> failing = forward_array(move thrown, false);
            array<u8> unexpected = await move failing;
            drop unexpected;
            return 21;
        } catch (transfer_error error) {
            if (error.code != 9) {
                return 22;
            }
        }

        array<u8> cancelled = std.alloc::bytes(1usize, 59u8);
        task<array<u8> throws transfer_error> abandoned = forward_array(move cancelled, true);
        std.async::cancel(move abandoned);

        array<u8> stopped = std.alloc::bytes(1usize, 73u8);
        try {
            task<void> operation = consume_array(move stopped);
            await move operation;
            return 23;
        } catch (std.async::start_error error) {
            if (error != std.async::start_error::runtime_stopping) {
                return 24;
            }
        }
        if (holds(&stopped, 73u8) == false) {
            return 25;
        }
        array<u8> uncommitted = std.alloc::bytes(1usize, 79u8);
        try {
            task<void> operation = consume_array(move uncommitted);
            await move operation;
            return 26;
        } catch (std.async::start_error error) {
            if (error != std.async::start_error::runtime_stopping) {
                return 27;
            }
        }
        if (holds(&uncommitted, 79u8) == false) {
            return 28;
        }
        drop uncommitted;
        drop stopped;
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 30;
    } catch (std.fs::path_error error) {
        error as void;
        return 31;
    } catch (std.async::start_error error) {
        error as void;
        return 32;
    } catch (transfer_error error) {
        error as void;
        return 33;
    }
}
