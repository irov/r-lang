module test.codegen.expression_forms;

/* L37: small expression and statement forms. Each part checks values, moves and the exact
   destruction of owned parts through a drop hook that sums the markers it sees. */

protected i32 drop_trace(i32 marker) {
    static i32 trace = 0;
    unsafe {
        if (marker != 0) { trace = trace + marker; }
        return trace;
    }
}

struct Token { i32 marker; };

drop(Token* self) {
    i32 ignored = drop_trace(self->marker);
    ignored as void;
}

protected (i32, Token, bool) make(i32 value) {
    return (value, Token {.marker = value * 10}, true);
}

/* R-STMT-0022 (L37.3): a destructuring declaration moves or copies each element into a local of
   the block; the emptied tuple destroys nothing. */
protected i32 destructuring() {
    i32 status = 0;
    i32 before = drop_trace(0);
    auto (count, token, flag) = make(4);
    if (count != 4) { status = 1; }
    if (flag != true) { status = 2; }
    i32 seen = token.marker;
    if (seen != 40) { status = 3; }
    if (drop_trace(0) != before) { status = 4; }
    drop token;
    if (drop_trace(0) != before + 40) { status = 5; }
    (i32, Token, bool) kept = make(5);
    auto (n, t, f) = move kept;
    if (n != 5) { status = 6; }
    if (f != true) { status = 7; }
    if (t.marker != 50) { status = 8; }
    // In a switch clause, the locals belong to the clause.
    i32 picked = 0;
    switch (picked) {
    case 0:
        auto (low, high) = (3, 4);
        picked = low * high;
    default: picked = -1;
    }
    if (picked != 12) { status = 10; }
    // t is destroyed at the end of the block, once.
    return status;
}

struct Pair { Token left; Token right; i32 tag; };

struct Version { u32 major; u32 minor; u32 patch; };

/* R-INIT-0004 (L37.4): `...base` completes a struct initializer: the omitted fields move or copy
   out of the base, and a field given explicitly destroys its old value once. */
protected i32 updating() {
    i32 status = 0;
    i32 before = drop_trace(0);
    Pair original = Pair {.left = Token {.marker = 1}, .right = Token {.marker = 2}, .tag = 7};
    Pair changed = Pair {.left = Token {.marker = 100}, ...move original};
    if (drop_trace(0) != before + 1) { status = 11; }
    if (changed.left.marker != 100) { status = 12; }
    if (changed.right.marker != 2) { status = 13; }
    if (changed.tag != 7) { status = 14; }
    // A Copy base stays usable.
    Version first = Version {.major = 1u32, .minor = 2u32, .patch = 3u32};
    Version second = Version {.patch = 4u32, ...first};
    if (second.major != 1u32 || second.minor != 2u32 || second.patch != 4u32) { status = 15; }
    if (first.patch != 3u32) { status = 16; }
    // The base may be any expression of the type; a temporary is destroyed the same way.
    Pair third = Pair {.tag = 8, ...Pair {.left = Token {.marker = 1000}, .right = Token {.marker = 2000}, .tag = 0}};
    if (third.tag != 8 || third.left.marker != 1000) { status = 17; }
    return status;
}

/* The first multiple of `step` among row * column for 1 <= column < row < 10, as
   row * 100 + column; also evaluated during translation (R-FUNC-0023). */
protected i32 first_multiple(i32 step) {
    i32 found = 0;
    rows: for (i32 row = 1; row < 10; row += 1) {
        for (i32 column = 1; column < 10; column += 1) {
            if (column == row) { continue rows; }
            if ((row * column) % step == 0) {
                found = row * 100 + column;
                break rows;
            }
        }
    }
    return found;
}

const i32 FIRST_MULTIPLE = first_multiple(7);

/* R-STMT-0004 (L37.2): `break name;` and `continue name;` leave or continue a labeled loop
   from a nested loop or switch, destroying what the left scopes own. */
protected i32 labeled() {
    i32 status = 0;
    u32 found_row = 99u32;
    u32 found_column = 99u32;
    outer: for (u32 row = 0u32; row < 5u32; row += 1u32) {
        for (u32 column = 0u32; column < 5u32; column += 1u32) {
            if (row * column == 6u32) {
                found_row = row;
                found_column = column;
                break outer;
            }
        }
    }
    if (found_row != 2u32 || found_column != 3u32) { status = 21; }
    u32 visited = 0u32;
    rows: for (u32 row = 0u32; row < 4u32; row += 1u32) {
        for (u32 column = 0u32; column < 4u32; column += 1u32) {
            switch (column) {
            case 2u32: continue rows;
            default: visited += 1u32;
            }
        }
        visited += 100u32;
    }
    if (visited != 8u32) { status = 22; }
    i32 before = drop_trace(0);
    u32 rounds = 0u32;
    holding: while (true) {
        Token held = Token {.marker = 7};
        while (true) {
            rounds += 1u32;
            if (rounds == 3u32) { break holding; }
        }
        drop held;
    }
    if (rounds != 3u32) { status = 23; }
    if (drop_trace(0) != before + 7) { status = 24; }
    u32 count = 0u32;
    direct: while (count < 10u32) {
        count += 1u32;
        if (count == 4u32) { break direct; }
        continue direct;
    }
    if (count != 4u32) { status = 25; }
    // The jump leaves the scopes between with their drops; the states after the inner loop are
    // those of its own exits.
    u32 passes = 0u32;
    moves: for (u32 pass = 0u32; pass < 3u32; pass += 1u32) {
        Token carried = Token {.marker = 100};
        for (u32 inner = 0u32; inner < 2u32; inner += 1u32) {
            if (pass == 1u32) {
                drop carried;
                continue moves;
            }
        }
        passes += 1u32;
        drop carried;
    }
    if (passes != 2u32) { status = 26; }
    // A finally clause between runs on the way out.
    i32 finals = 0;
    guarded: for (i32 round = 0; round < 5; round += 1) {
        try {
            for (i32 k = 0; k < 5; k += 1) {
                if (round == 2) { break guarded; }
            }
        } finally {
            finals += 1;
        }
    }
    if (finals != 3) { status = 27; }
    // From a switch, `break name;` leaves the loop around it.
    u32 stops = 0u32;
    picking: for (u32 n = 0u32; n < 10u32; n += 1u32) {
        switch (n) {
        case 4u32: break picking;
        default: stops += 1u32;
        }
    }
    if (stops != 4u32) { status = 28; }
    if (first_multiple(7) != FIRST_MULTIPLE || FIRST_MULTIPLE != 701) { status = 20; }
    return status;
}

enum Packet { empty, data { i32 amount; o<i32> extra; } };

protected o<Token> wrap(i32 marker) { return o::some(Token {.marker = marker}); }

protected o<Token> nothing() { return o::none; }

/* R-STMT-0002 (L37.1): `if (value is pattern)` tests a match pattern; its bindings exist in the
   selected block, read-only for a place and moved out of an owned value. */
protected i32 patterns() {
    i32 status = 0;
    o<i32> maybe = o::some(5);
    if (maybe is variant o::some(value)) {
        if (value != 5) { status = 31; }
    } else {
        status = 32;
    }
    if (maybe is variant o::none) { status = 33; }
    Packet packet = Packet::data {.amount = 7, .extra = o::some(2)};
    if (packet is variant Packet::data { .amount = n, .extra = variant o::some(x) }) {
        if (n + x != 9) { status = 34; }
    } else {
        status = 35;
    }
    if (packet is variant Packet::empty) { status = 40; }
    i32 before = drop_trace(0);
    if (wrap(9) is variant o::some(move token)) {
        if (token.marker != 9) { status = 36; }
    }
    if (drop_trace(0) != before + 9) { status = 37; }
    if (nothing() is variant o::some(move missing)) {
        status = 38;
        drop missing;
    }
    o<Token> held = o::some(Token {.marker = 20});
    if (move held is variant o::some(move inner)) {
        if (inner.marker != 20) { status = 41; }
    }
    if (drop_trace(0) != before + 29) { status = 39; }
    return status;
}

struct Feed { i32 left; };

// Tokens marked left, left - 1, ..., 1, then none.
protected o<Token> take(Feed* feed) {
    if (feed->left == 0) { return o::none; }
    i32 marker = feed->left;
    feed->left -= 1;
    return o::some(Token {.marker = marker});
}

/* `while (value is pattern)` tests the pattern before each iteration and ends the loop when it
   does not match; the tested value of each iteration is destroyed with it. */
protected i32 pattern_loops() {
    i32 status = 0;
    Feed feed = Feed {.left = 4};
    i32 sum = 0;
    while (take(&feed) is variant o::some(move token)) {
        sum += token.marker;
    }
    if (sum != 10 || feed.left != 0) { status = 51; }
    o<i32> cursor = o::some(3);
    i32 steps = 0;
    while (cursor is variant o::some(value)) {
        steps += value;
        if (value == 1) { cursor = o::none; } else { cursor = o::some(value - 1); }
    }
    if (steps != 6) { status = 52; }
    Feed second = Feed {.left = 9};
    i32 seen = 0;
    scan: while (take(&second) is variant o::some(move token)) {
        for (i32 i = 0; i < 3; i += 1) {
            if (token.marker == 7) { continue scan; }
            if (token.marker == 5) { break scan; }
        }
        seen += token.marker;
    }
    // 9 and 8 were added, 7 was skipped and 5 ended the loop; 9 + 8 + 7 + 6 + 5 tokens died.
    if (seen != 23 || second.left != 4) { status = 53; }
    return status;
}

// Each part returns zero or a code; the drop trace after a part shows what it destroyed.
protected i32 checked(i32 status, i32 before, i32 destroyed, i32 code) {
    if (status != 0) { return status; }
    if (drop_trace(0) != before + destroyed) { return code; }
    return 0;
}

i32 main() {
    i32 before = drop_trace(0);
    i32 first = checked(destructuring(), before, 90, 9);
    if (first != 0) { return first; }
    i32 after_update = drop_trace(0);
    i32 second = checked(updating(), after_update, 3103, 19);
    if (second != 0) { return second; }
    i32 after_labels = drop_trace(0);
    i32 third = checked(labeled(), after_labels, 307, 29);
    if (third != 0) { return third; }
    i32 after_patterns = drop_trace(0);
    i32 fourth = checked(patterns(), after_patterns, 29, 49);
    if (fourth != 0) { return fourth; }
    i32 after_loops = drop_trace(0);
    i32 fifth = checked(pattern_loops(), after_loops, 45, 59);
    return fifth;
}
