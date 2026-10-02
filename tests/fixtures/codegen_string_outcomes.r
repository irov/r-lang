module test.codegen.string_outcomes;

i32 inspect(bytes data, bool valid) {
    std.string::from_bytes_result outcome = std.string::from_bytes(move data);
    switch (move outcome) {
    case variant std.string::from_bytes_result::valid(move text):
        if (valid == false) { return 1; }
        bytes recovered = std.string::into_bytes(move text);
        if (std.bytes::equal(recovered, "hello") == false) { return 2; }
        return 0;
    case variant std.string::from_bytes_result::invalid(move failure):
        if (valid == true || failure.index != 1usize || len(failure.bytes) != 2usize) { return 3; }
        const u8[] source = std.array::as_slice(&failure.bytes);
        if (source[0] != 65u8 || source[1] != 255u8) { return 4; }
        return 0;
    }
}

/* L37.5 (R-STMT-0010): a switch on a place of the outcome borrows it. Its bindings are shared
   borrows of the payload, and the outcome keeps what it owns. */
i32 peek(std.string::from_bytes_result* outcome) {
    switch (*outcome) {
    case variant std.string::from_bytes_result::valid(text):
        if (std.string::len(text) != 5usize) { return 11; }
        return 0;
    case variant std.string::from_bytes_result::invalid(failure):
        if (failure->index != 1usize || len(failure->bytes) != 2usize) { return 12; }
        return 0;
    }
}

i32 borrowed(bytes data, bool valid) {
    std.string::from_bytes_result outcome = std.string::from_bytes(move data);
    i32 seen = 0;
    switch (outcome) {
    case variant std.string::from_bytes_result::valid(text): seen = 1;
    case variant std.string::from_bytes_result::invalid(failure): seen = 2;
    }
    i32 peeked = peek(&outcome);
    if (peeked != 0) { return peeked; }
    // Still owned: a moving switch consumes it and sees the same payload.
    switch (move outcome) {
    case variant std.string::from_bytes_result::valid(move text):
        if (valid == false || seen != 1 || std.string::len(&text) != 5usize) { return 13; }
    case variant std.string::from_bytes_result::invalid(move failure):
        if (valid == true || seen != 2 || len(failure.bytes) != 2usize) { return 14; }
    }
    return 0;
}

// The same in an asynchronous function, whose bindings live in its frame or resume stack.
async i32 borrowed_async(bytes data, bool valid) {
    std.string::from_bytes_result outcome = std.string::from_bytes(move data);
    i32 seen = 0;
    switch (outcome) {
    case variant std.string::from_bytes_result::valid(text):
        if (std.string::len(text) == 5usize) { seen = 1; }
    case variant std.string::from_bytes_result::invalid(failure):
        if (failure->index == 1usize && len(failure->bytes) == 2usize) { seen = 2; }
    }
    switch (move outcome) {
    case variant std.string::from_bytes_result::valid(move text):
        if (valid == false || seen != 1 || std.string::len(&text) != 5usize) { return 15; }
    case variant std.string::from_bytes_result::invalid(move failure):
        if (valid == true || seen != 2 || len(failure.bytes) != 2usize) { return 16; }
    }
    return 0;
}

bytes invalid() throws std.alloc::alloc_error {
    bytes data = {};
    std.bytes::append_u8(&data, 65u8);
    std.bytes::append_u8(&data, 255u8);
    return move data;
}

async i32 main() {
    try {
    std.string::string greeting = std.string::from_str("hello");
    bytes good = std.string::into_bytes(move greeting);
    if (inspect(move good, true) != 0) { return 5; }
    bytes bad = invalid();
    if (inspect(move bad, false) != 0) { return 6; }
    std.string::string again = std.string::from_str("hello");
    i32 kept = borrowed(std.string::into_bytes(move again), true);
    if (kept != 0) { return kept; }
    i32 kept_invalid = borrowed(invalid(), false);
    if (kept_invalid != 0) { return kept_invalid; }
    std.string::string third = std.string::from_str("hello");
    i32 awaited = await borrowed_async(std.string::into_bytes(move third), true);
    if (awaited != 0) { return awaited; }
    i32 awaited_invalid = await borrowed_async(invalid(), false);
    if (awaited_invalid != 0) { return awaited_invalid; }
    bytes input = invalid();
    std.string::from_bytes_result result = std.string::from_bytes(move input);
    switch (move result) {
    case variant std.string::from_bytes_result::valid(move text): return 7;
    case variant std.string::from_bytes_result::invalid(move failure):
        if (failure.index != 1usize || len(failure.bytes) != 2usize) { return 8; }
        break;
    }
    return 0;
    } catch (std.alloc::alloc_error failure) { return 9; }
}
