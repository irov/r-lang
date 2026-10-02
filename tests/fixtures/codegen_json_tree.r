module test.codegen.json_tree;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { std.json::value value; };

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = std.string::as_bytes(source);
    usize size = len(actual);
    usize other_size = len(expected);
    if (size != other_size) { return false; }
    usize index = 0;
    while (index < size) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

protected i32 exercise() throws std.json::error, std.alloc::alloc_error {
    TestStorage1 storage_value = {.value = std.json::parse("{\"id\":18446744073709551615,\"name\":\"Alice\"}")};
    usize count = std.json::len(&storage_value.value);
    if (count != 2) { return 1; }
    std.string::string text = std.json::stringify(&storage_value.value);
    bool correct = matches(&text, "{\"id\":18446744073709551615,\"name\":\"Alice\"}");
    if (correct == false) { return 2; }
    i32 caught = 0;
    try { storage_value.value = std.json::parse("{\"id\":1,\"id\":2}"); }
    catch (std.json::error error) { caught += 1; }
    finally { caught += 1; }
    if (caught != 2) { return 3; }
    usize count_2 = std.json::len(&storage_value.value);
    if (count_2 != 2) { return 4; }
    std.json::number number = std.json::parse_number("-0.00e-9999999999999999");
    bool zero = std.json::number_is_zero(&number);
    if (zero == false) { return 5; }
    str number_source = std.json::number_text(&number);
    if (len(number_source) != 23usize) { return 6; }
    std.json::value numeric = std.json::from_number(&number);
    std.string::string spelling = std.json::stringify(&numeric);
    bool correct_2 = matches(&spelling, "-0.00e-9999999999999999");
    if (correct_2 == false) { return 7; }
    std.json::value empty_array = std.json::array();
    if (std.json::len(&empty_array) != 0usize) { return 8; }
    return 0;
}

i32 main() {
    try {
        try { i32 status = exercise(); return status; }
        catch (std.alloc::alloc_error error) { throw TestAssertionFailed {.code = 99}; }
        catch (std.json::error error) { throw TestAssertionFailed {.code = 98}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
