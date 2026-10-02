module test.codegen.secret_buffer;

protected bool same_secret(const std.secret::buffer* left, const std.secret::buffer* right) {
    const u8[] left_view = std.secret::as_slice(left);
    const u8[] right_view = std.secret::as_slice(right);
    return std.secret::constant_time_equal(left_view, right_view);
}

protected i32 sync_checks() {
    try {
        std.secret::buffer key = std.secret::with_length(4);
        if (std.secret::len(&key) != 4usize) {
            drop key;
            return 1;
        }
        const u8[] fresh = std.secret::as_slice(&key);
        if ((len(fresh) != 4usize) || (fresh[0] != 0) || (fresh[3] != 0)) {
            drop key;
            return 2;
        }
        u8[] writable = std.secret::as_slice_mut(&key);
        writable[0] = 0x10;
        writable[1] = 0x20;
        writable[2] = 0x30;
        writable[3] = 0x40;

        bytes staged = {};
        std.bytes::append_u8(&staged, 0x10);
        std.bytes::append_u8(&staged, 0x20);
        std.bytes::append_u8(&staged, 0x30);
        std.bytes::append_u8(&staged, 0x40);
        std.secret::buffer adopted = std.secret::from_bytes(move staged);
        if (std.secret::len(&adopted) != 4usize) {
            drop adopted;
            drop key;
            return 3;
        }
        if (same_secret(&key, &adopted) == false) {
            drop adopted;
            drop key;
            return 4;
        }

        u8[] adopted_view = std.secret::as_slice_mut(&adopted);
        adopted_view[3] = 0x41;
        if (same_secret(&key, &adopted) == true) {
            drop adopted;
            drop key;
            return 5;
        }

        std.secret::buffer empty = std.secret::with_length(0);
        if ((std.secret::len(&empty) != 0usize) || (same_secret(&key, &empty) == true)) {
            drop empty;
            drop adopted;
            drop key;
            return 6;
        }
        drop empty;

        u8[4] scratch = {0xaa, 0xbb, 0xcc, 0xdd};
        u8[] scratch_view = &scratch;
        std.secret::zeroize(scratch_view);
        if ((scratch[0] != 0) || (scratch[1] != 0) || (scratch[2] != 0) || (scratch[3] != 0)) {
            drop adopted;
            drop key;
            return 7;
        }
        u8[] key_view = std.secret::as_slice_mut(&key);
        std.secret::zeroize(key_view);
        const u8[] erased = std.secret::as_slice(&key);
        if ((erased[0] != 0) || (erased[3] != 0)) {
            drop adopted;
            drop key;
            return 8;
        }
        drop adopted;
        drop key;
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 9;
    }
}

protected i32 consume(std.secret::buffer owned) {
    i32 outcome = 0;
    if (std.secret::len(&owned) != 2usize) {
        outcome = 10;
    }
    drop owned;
    return outcome;
}

i32 main() {
    try {
        i32 outcome = sync_checks();
        if (outcome != 0) {
            return outcome;
        }
        try {
            std.secret::buffer moved = std.secret::with_length(2);
            i32 consumed = consume(move moved);
            return consumed;
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 11};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
