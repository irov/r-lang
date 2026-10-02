module test.codegen.async_secret_buffer;

protected i32 consume(std.secret::buffer owned) {
    i32 outcome = 0;
    if (std.secret::len(&owned) != 3usize) {
        outcome = 10;
    }
    drop owned;
    return outcome;
}

async i32 main() {
    try {
        try {
            std.secret::buffer key = std.secret::with_length(3);
            if (std.secret::len(&key) != 3usize) {
                drop key;
                throw TestAssertionFailed {.code = 1};
            }
            u8[] writable = std.secret::as_slice_mut(&key);
            writable[0] = 0x01;
            writable[1] = 0x02;
            writable[2] = 0x03;
            const u8[] view = std.secret::as_slice(&key);
            if ((len(view) != 3usize) || (view[2] != 0x03)) {
                drop key;
                throw TestAssertionFailed {.code = 2};
            }

            bytes staged = {};
            std.bytes::append_u8(&staged, 0x01);
            std.bytes::append_u8(&staged, 0x02);
            std.bytes::append_u8(&staged, 0x03);
            std.secret::buffer adopted = std.secret::from_bytes(move staged);
            const u8[] adopted_view = std.secret::as_slice(&adopted);
            if (std.secret::constant_time_equal(view, adopted_view) == false) {
                drop adopted;
                drop key;
                throw TestAssertionFailed {.code = 3};
            }
            u8[] adopted_writable = std.secret::as_slice_mut(&adopted);
            adopted_writable[0] = 0x09;
            const u8[] changed = std.secret::as_slice(&adopted);
            if (std.secret::constant_time_equal(view, changed) == true) {
                drop adopted;
                drop key;
                throw TestAssertionFailed {.code = 4};
            }

            u8[] erase_view = std.secret::as_slice_mut(&key);
            std.secret::zeroize(erase_view);
            const u8[] erased = std.secret::as_slice(&key);
            if ((erased[0] != 0) || (erased[1] != 0) || (erased[2] != 0)) {
                drop adopted;
                drop key;
                throw TestAssertionFailed {.code = 5};
            }
            drop key;
            i32 consumed = consume(move adopted);
            return consumed;
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 6};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
