module test.codegen.async_format_literals;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = *source;
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

struct TextPair { std.string::string first; std.string::string second; };
protected void consume(std.string::string first, std.string::string second) {}
@generic<T>
protected void consume_generic(T first, T second) {}

struct CNumbers { c_float small; c_long_double large; c_int number; c_bool flag; };
protected CNumbers c_numbers(f32 value) throws std.convert::range_error {
    c_float small = std.c::checked_c_float(value);
    c_long_double large = std.c::checked_c_long_double(value);
    c_int number = std.c::checked_c_int(-42);
    c_bool flag = std.c::checked_c_bool(1);
    return CNumbers {.small = small, .large = large, .number = number, .flag = flag};
}

error DynamicError {
    std.string::string message;
};

protected void fail(bool first, i32 count) throws DynamicError, std.alloc::alloc_error {
    throw (first == true) DynamicError {.message = f"first {count}"}
        else DynamicError {.message = f"second {count}"};
}

protected std.string::string returned(i32 count) throws std.alloc::alloc_error {
    std.string::string selected = (count == 99) ? f"yes {count}" : f"no {count}";
    return move selected;
}

protected async void pause() {}

protected async i32 exercise() throws std.alloc::alloc_error, std.async::start_error {
    i32 count = 42;
    TextPair pair = {.first = f"first", .second = f"second"};
    bool pair_correct = matches(&pair.first, "first");
    if (pair_correct == false) { return 14; }
    consume(f"call first", f"call second");
    consume_generic(f"generic first", f"generic second");

    std.string::string text = f"hello {count:08x}";
    bool correct = matches(&text, "hello 0000002a");
    if (correct == false) { return 1; }
    std.format::format f0 = f"test {count} {1} {2} {1}";
    i32 count_2 = 99;
    std.string::string other = f0.format(1, 2);
    bool correct_2 = matches(&other, "test 42 1 2 1");
    if (correct_2 == false) { return 2; }
    std.string::string another = f0.format("A", "B");
    bool correct_3 = matches(&another, "test 42 A B A");
    if (correct_3 == false) { return 3; }
    std.format::format moved = move f0;
    std.string::string last = moved.format(3, 4);
    bool correct_4 = matches(&last, "test 42 3 4 3");
    if (correct_4 == false) { return 4; }
    std.string::string name = std.string::from_str("Alice");
    std.format::format snapshot = f"hello {name} {1}";
    drop name;
    try { await pause(); }
    finally {
        try { std.string::string finalized_text = f"finalized";
        test_observe(&finalized_text); }
        catch (std.alloc::alloc_error ignored) {}
    }
    std.string::string saved = snapshot.format(7);
    bool correct_5 = matches(&saved, "hello Alice 7");
    if (correct_5 == false) { return 5; }
    try {
        std.string::string retry = snapshot.format(8);
        bool retry_correct = matches(&retry, "hello Alice 8");
        if (retry_correct == false) { return 15; }
    } catch (std.alloc::alloc_error failed) {
        std.string::string retry = snapshot.format(8);
        bool retry_correct = matches(&retry, "hello Alice 8");
        if (retry_correct == false) { return 15; }
    }
    f32 small = 1.375f32;
    try {
        CNumbers c = c_numbers(small);
        const CNumbers* cp = &c;
        std.string::string small_text = f"{small:.2}/{c.small:.2}/{c.large:.2}/{cp->number:08x}/{c.flag}";
        bool correct_small = matches(&small_text, "1.38/1.38/1.38/-000002a/true");
        if (correct_small == false) { return 16; }
    } catch (std.convert::range_error error) { return 17; }
    f64 fraction = 1.125;
    i32 negative = -42;
    bool flag = true;
    char scalar = '\u{1f642}';
    std.string::string numbers = f"{fraction:.2}/{negative:08X}/{flag}/{scalar}";
    bool correct_7 = matches(&numbers, "1.12/-000002A/true/\u{1f642}");
    if (correct_7 == false) { return 6; }
    std.string::string escaped = "ordinary {x}/" f"{{{count_2}}}/\u{7b}/\x7d";
    bool correct_8 = matches(&escaped, "ordinary {x}/{99}/{/}");
    if (correct_8 == false) { return 7; }
    std.string::string direct = f"{count_2}:{2}:{1}:{2}".format(10, 20);
    bool correct_9 = matches(&direct, "99:20:10:20");
    if (correct_9 == false) { return 8; }
    std.string::string nested = f"outer {1}".format(f"inner {count_2}");
    bool correct_10 = matches(&nested, "outer inner 99");
    if (correct_10 == false) { return 9; }
    std.string::string result = returned(count_2);
    bool correct_11 = matches(&result, "yes 99");
    if (correct_11 == false) { return 10; }
    bytes consumed = std.string::into_bytes(move result);
    usize consumed_length = len(consumed);
    if (consumed_length != 6) { return 11; }
    i32 finalized = 0;
    try {
        fail(false, count_2);
    } catch (DynamicError error) {
        correct_11 = matches(&error.message, "second 99");
        if (correct_11 == false) { return 12; }
    } finally {
        finalized += 1;
    }
    if (finalized != 1) { return 13; }
    return 0;
}

async i32 main() {
    try {
        try { i32 status = await exercise(); return status; }
        catch (std.alloc::alloc_error error) { throw TestAssertionFailed {.code = 99}; }
        catch (std.async::start_error error) { throw TestAssertionFailed {.code = 98}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
