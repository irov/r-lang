module test.codegen.async_json_stream;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { std.json::feed_result value; };
struct TestStorage2 { std.json::feed_result value; };

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = *source;
    if (len(actual) != len(expected)) { return false; }
    usize index = 0;
    while (index < len(actual)) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

protected async i32 exercise() throws std.json::error, std.alloc::alloc_error {
    std.json::decoder<array<o<std.string::string>>> decoder = std.json::new_decoder();
    std.string::string source_owner = std.string::from_str("[\"Madrid\",null,\"\\uD83D\\uDE00\"]");
    const u8[] source = source_owner;
    usize index = 0;
    while (index < len(source)) {
        usize end = index + 1;
        std.json::feed_result progress = std.json::feed(&decoder, source[index..end], false);
        if (progress.consumed != 1 || progress.state != std.json::feed_state::need_input) { return 1; }
        index = end;
    }
    std.json::feed_result ready = std.json::feed(&decoder, "", true);
    if (ready.state != std.json::feed_state::value_ready) { return 2; }
    array<o<std.string::string>> names = std.json::take(&decoder);
    std.string::string encoded = std.json::marshal(&names);
    if (matches(&encoded, "[\"Madrid\",null,\"😀\"]") == false) { return 3; }
    std.json::feed_result ready_2 = std.json::feed(&decoder, "", true);
    if (ready_2.state != std.json::feed_state::end) { return 4; }

    std.json::decoder<list<i32>> numbers = std.json::new_decoder();
    std.json::feed_result ready_3 = std.json::feed(&numbers, "[1,", false);
    ready_3 as void;
    std.json::feed_result ready_4 = std.json::feed(&numbers, "-2,3]", true);
    ready_4 as void;
    list<i32> result = std.json::take(&numbers);
    std.string::string encoded_2 = std.json::marshal(&result);
    drop encoded;
    if (matches(&encoded_2, "[1,-2,3]") == false) { return 5; }

    std.json::decoder<i32[3]> fixed = std.json::new_decoder();
    std.json::feed_result ready_5 = std.json::feed(&fixed, "[1,2,3]", true);
    ready_5 as void;
    i32[3] fixed_result = std.json::take(&fixed);
    if (fixed_result[2] != 3) { return 6; }

    std.json::options options = {
        .max_depth = 256, .max_value_bytes = 16, .indent = 0,
        .reject_unknown_fields = false, .ignore_case = false,
        .mode = std.json::mode::array_elements,
    };
    std.json::decoder<std.json::value> elements = std.json::new_decoder(options);
    test_observe(&elements);
    std.string::string documents_owner = std.string::from_str("[null,{},[1],true,\"s\"]");
    const u8[] documents = documents_owner;
    usize index_2 = 0;
    usize count = 0;
    usize document_size = len(documents);
    bool ended = false;
    while (ended == false) {
        std.json::feed_result current = std.json::feed(&elements, documents[index_2..document_size], true);
        index_2 += current.consumed;
        if (current.state == std.json::feed_state::value_ready) {
            std.json::value value = std.json::take(&elements);
            test_observe(&value);
            count += 1;
        } else {
            if (current.state != std.json::feed_state::end) { return 7; }
            ended = true;
        }
    }
    if (count != 5 || index_2 != len(documents)) { return 8; }
    return 0;
}


protected i32 default_count(i32 increment) {
    static i32 count = 0;
    unsafe { count += increment; return count; }
}
protected i32 next_default() { i32 count = default_count(1); return count; }
struct Address { @json(case = "ignore") std.string::string city; };
struct Envelope {
    @json(embed) Address address;
    @json(name = "user_id", string) u64 id;
    @json(optional, omitempty) std.string::string email;
    @json(optional, default = next_default) i32 serial;
    @json(skip, default = 42) i32 internal;
    @json(embed) std.json::value extra;
};
struct Plain { i32 n; };
@generic<T> struct Box { T value; };

protected async i32 exercise_struct() throws std.json::error, std.alloc::alloc_error {
    i32 before = default_count(0);
    std.json::decoder<Envelope> decoder = std.json::new_decoder();
    std.json::feed_result progress = std.json::feed(
        &decoder, "{\"user_id\":\"18446744073709551615\",\"C_I-TY\":\"Madrid\",\"extra\":[1,", false);
    progress as void;
    i32 middle = default_count(0);
    if (middle != before || progress.state != std.json::feed_state::need_input) { return 10; }
    std.json::feed_result progress_2 = std.json::feed(&decoder, "{\"owned\":\"value\"}]}", true);
    progress_2 as void;
    Envelope result = std.json::take(&decoder);
    test_observe(&result);
    i32 middle_2 = default_count(0);
    if (middle_2 != before + 1 || result.serial != middle_2 || result.internal != 42) { return 11; }
    if (result.id != 18446744073709551615u64) { return 12; }
    if (matches(&result.address.city, "Madrid") == false) { return 13; }
    std.string::string extras = std.json::stringify(&result.extra);
    if (matches(&extras, "{\"extra\":[1,{\"owned\":\"value\"}]}") == false) { return 14; }

    std.json::decoder<Box<Plain>> skipped = std.json::new_decoder();
    std.json::feed_result progress_3 = std.json::feed(&skipped, "{\"value\":{\"n\":7},\"ignored\":{\"nested\":[null,", false);
    progress_3 as void;
    TestStorage1 storage_progress_4 = {.value = std.json::feed(&skipped, "true,{\"text\":\"ok\"}]}}", true)};
    Box<Plain> plain = std.json::take(&skipped);
    if (plain.value.n != 7) { return 15; }
    std.json::decoder<Plain> damaged = std.json::new_decoder();
    try {
        storage_progress_4.value = std.json::feed(&damaged, "{\"n\":1,\"ignored\":{\"x\":0,\"x\":1}}", true);
        return 16;
    } catch (std.json::error error) {
        if (error.code != std.json::error_code::duplicate_key) { return 17; }
    }
    try { storage_progress_4.value = std.json::feed(&damaged, "{\"n\":2}", true); return 18; }
    catch (std.json::error error) {
        if (error.code != std.json::error_code::invalid_state) { return 19; }
    }
    return 0;
}


@generic<T> error Choice { None, Some(T), Fields { T name; }, };
struct Extras { @json(embed) dict<std.string::string, list<i32>> extra; };
struct Measure { usize count; };
struct Custom { @json(string) Measure size; };
protected i32 hook_count(i32 increment) {
    static i32 count = 0;
    unsafe { count += increment; return count; }
}
Measure Measure::json_unmarshal(const std.json::value* value)
    throws std.json::error, std.alloc::alloc_error {
    i32 called = hook_count(1);
    called as void;
    std.json::value_kind kind = std.json::kind(value);
    if (kind != std.json::value_kind::number) {
        i32 invalid = std.json::unmarshal("false");
        invalid as void;
    }
    Measure result = {.count = 7};
    return result;
}
struct Owned { std.string::string value; };
protected i32 drop_count(i32 increment) {
    static i32 count = 0;
    unsafe { count += increment; return count; }
}
drop(Owned* self) { i32 dropped = drop_count(1);
dropped as void; }

protected async i32 exercise_variants() throws std.json::error, std.alloc::alloc_error {
    std.json::decoder<array<Choice<std.string::string>>> decoder = std.json::new_decoder();
    std.json::feed_result progress = std.json::feed(&decoder, "[\"None\",{\"Some\":\"owned\"},{\"Fields\":{\"name\":", false);
    progress as void;
    std.json::feed_result progress_5 = std.json::feed(&decoder, "\"field\"}}]", true);
    progress_5 as void;
    array<Choice<std.string::string>> result = std.json::take(&decoder);
    std.string::string encoded = std.json::marshal(&result);
    if (matches(&encoded, "[\"None\",{\"Some\":\"owned\"},{\"Fields\":{\"name\":\"field\"}}]") == false) { return 20; }

    std.json::decoder<dict<std.string::string, list<i32>>> dictionary = std.json::new_decoder();
    std.json::feed_result progress_6 = std.json::feed(&dictionary, "{\"one\":[1,2],\"two\":[", false);
    progress_6 as void;
    std.json::feed_result progress_7 = std.json::feed(&dictionary, "3,4]}", true);
    progress_7 as void;
    dict<std.string::string, list<i32>> map = std.json::take(&dictionary);
    std.string::string encoded_3 = std.json::marshal(&map);
    drop encoded;
    if (matches(&encoded_3, "{\"one\":[1,2],\"two\":[3,4]}") == false) { return 21; }

    std.json::decoder<Extras> collector = std.json::new_decoder();
    std.json::feed_result progress_8 = std.json::feed(&collector, "{\"one\":[1,2],\"two\":[", false);
    progress_8 as void;
    std.json::feed_result progress_9 = std.json::feed(&collector, "3,4]}", true);
    progress_9 as void;
    Extras collected = std.json::take(&collector);
    std.string::string encoded_4 = std.json::marshal(&collected);
    drop encoded_3;
    if (matches(&encoded_4, "{\"one\":[1,2],\"two\":[3,4]}") == false) { return 22; }

    i32 before = hook_count(0);
    std.json::decoder<Custom> custom = std.json::new_decoder();
    std.json::feed_result progress_10 = std.json::feed(&custom, "{\"size\":\"12", false);
    progress_10 as void;
    i32 during = hook_count(0);
    if (during != before) { return 23; }
    std.json::feed_result progress_11 = std.json::feed(&custom, "3\"}", true);
    progress_11 as void;
    Custom measured = std.json::take(&custom);
    measured as void;
    i32 after = hook_count(0);
    if (after != before + 1 || measured.size.count != 7) { return 24; }

    i32 before_2 = drop_count(0);
    std.json::decoder<Owned> complete = std.json::new_decoder();
    std.json::feed_result progress_12 = std.json::feed(&complete, "{\"value\":\"owned\"}", true);
    progress_12 as void;
    drop complete;
    i32 after_2 = drop_count(0);
    if (after_2 != before_2 + 1) { return 25; }
    i32 before_3 = after_2;
    std.json::decoder<Owned> partial = std.json::new_decoder();
    std.json::feed_result progress_13 = std.json::feed(&partial, "{\"value\":\"partial\"", false);
    progress_13 as void;
    drop partial;
    i32 after_3 = drop_count(0);
    if (after_3 != before_3) { return 26; }
    std.json::decoder<array<Owned>> children = std.json::new_decoder();
    std.json::feed_result progress_14 = std.json::feed(&children, "[{\"value\":\"child\"},", false);
    progress_14 as void;
    drop children;
    i32 after_4 = drop_count(0);
    if (after_4 != before_3 + 1) { return 27; }
    return 0;
}

struct Leaf { std.string::string name; array<i32> children; };
struct Node { std.string::string name; array<Leaf> children; };

protected async i32 exercise_recursive() throws std.json::error, std.alloc::alloc_error {
    std.string::string source_owner = std.string::from_str("{\"name\":\"root\",\"children\":[{\"name\":\"leaf\",\"children\":[]}]}");
    const u8[] source = source_owner;
    Node tree = std.json::unmarshal(source);
    std.string::string encoded = std.json::marshal(&tree);
    if (matches(&encoded, source) == false) { return 28; }
    std.json::decoder<Node> decoder = std.json::new_decoder();
    usize index = 0;
    while (index < len(source)) {
        usize end = index + 1;
        std.json::feed_result progress = std.json::feed(&decoder, source[index..end], false);
        if (progress.consumed != 1) { return 29; }
        index = end;
    }
    TestStorage2 storage_ready = {.value = std.json::feed(&decoder, "", true)};
    Node streamed = std.json::take(&decoder);
    std.string::string encoded_5 = std.json::marshal(&streamed);
    drop encoded;
    if (matches(&encoded_5, source) == false) { return 30; }
    std.json::options options = {
        .max_depth = 2, .max_value_bytes = 67108864, .indent = 0,
        .reject_unknown_fields = false, .ignore_case = false,
        .mode = std.json::mode::document,
    };
    try { Node rejected = std.json::unmarshal_with_options(source, options);
    test_observe(&rejected); return 31; }
    catch (std.json::error failure) {
        if (failure.code != std.json::error_code::depth_limit) { return 32; }
    }
    try { std.string::string rejected = std.json::marshal_with_options(&tree, options);
    test_observe(&rejected); return 33; }
    catch (std.json::error failure) {
        if (failure.code != std.json::error_code::depth_limit) { return 34; }
    }
    std.json::decoder<Node> damaged = std.json::new_decoder();
    try {
        storage_ready.value = std.json::feed(&damaged, "{\"name\":\"root\",\"children\":[{\"name\":\"leaf\",\"children\":[]}]}!", true);
        return 35;
    } catch (std.json::error failure) {
        if (failure.code != std.json::error_code::syntax) { return 36; }
    }
    return 0;
}

struct PrivateState { constexpr str label; };
protected PrivateState private_default() { PrivateState value = {.label = "factory"}; return value; }
struct Excluded {
    i32 id;
    @json(skip) PrivateState empty;
    @json(skip, default = private_default) PrivateState custom;
};
struct ExcludedEmbedded { @json(embed) Excluded child; };
protected async i32 exercise_excluded() throws std.json::error, std.alloc::alloc_error {
    Excluded value = std.json::unmarshal("{\"id\":1,\"empty\":[1,2],\"custom\":{\"ignored\":true}}");
    std.string::string custom = std.string::from_str(value.custom.label);
    if (matches(&custom, "factory") == false) { return 37; }
    std.string::string empty = std.string::from_str(value.empty.label);
    if (matches(&empty, "") == false) { return 38; }
    std.json::decoder<ExcludedEmbedded> decoder = std.json::new_decoder();
    std.json::feed_result ready = std.json::feed(&decoder, "{\"id\":1}", true);
    ready as void;
    ExcludedEmbedded streamed = std.json::take(&decoder);
    std.string::string encoded = std.json::marshal(&streamed);
    if (matches(&encoded, "{\"id\":1}") == false) { return 39; }
    std.string::string custom_2 = std.string::from_str(streamed.child.custom.label);
    drop custom;
    if (matches(&custom_2, "factory") == false) { return 40; }
    return 0;
}

async i32 main() {
    try {
        try { i32 status = await exercise(); if (status != 0) { return status; } i32 other = await exercise_struct(); if (other != 0) { return other; } i32 last = await exercise_variants(); if (last != 0) { return last; } i32 recursive = await exercise_recursive(); if (recursive != 0) { return recursive; } i32 excluded = await exercise_excluded(); return excluded; }
        catch (std.alloc::alloc_error error) { throw TestAssertionFailed {.code = 99}; }
        catch (std.json::error error) { throw TestAssertionFailed {.code = 98}; }
        catch (std.async::start_error error) { throw TestAssertionFailed {.code = 97}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
