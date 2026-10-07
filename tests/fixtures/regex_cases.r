module test.regex_cases;

import std.regex;
import std.text;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { o<std.regex::span> value; };
struct TestStorage2 { bool value; };

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


protected bool expect(str pattern, str text, i32 begin, i32 end, bool whole)
    throws std.regex::error, std.alloc::alloc_error {
    std.regex::regex expression = std.regex::compile(pattern);
    bool full = std.regex::full_match(&expression, text);
    if (full != whole) { return false; }
    bool any = std.regex::is_match(&expression, text);
    if (any != (begin >= 0)) { return false; }
    o<std.regex::span> result = std.regex::find(&expression, text);
    switch (result) {
    case variant o::some(value):
        return begin >= 0 && value->start == (begin as usize) && value->end == (end as usize);
    case variant o::none: return begin < 0;
    }
}

protected bool invalid(str pattern, std.regex::error_code expected) throws std.alloc::alloc_error {
    try {
        std.regex::regex expression = std.regex::compile(pattern);
        test_observe(&expression);
    } catch (std.regex::error failure) {
        return failure.code == expected;
    }
    return false;
}

protected i32 edge_cases() throws std.regex::error, std.alloc::alloc_error {
    std.string::string nested = std.string::create();
    usize index = 0usize;
    while (index < 128usize) { std.string::append_str(&nested, "("); index += 1usize; }
    usize index_2 = 0usize;
    while (index_2 < 128usize) { std.string::append_str(&nested, ")"); index_2 += 1usize; }
    str nested_text = nested;
    bool valid = expect(nested_text, "", 0, 0, true);
    if (valid == false) { return 90; }
    std.string::string too_deep = std.string::from_str("(");
    std.string::append_str(&too_deep, nested_text);
    str deep_text = too_deep;
    bool valid_2 = invalid(deep_text, std.regex::error_code::program_limit);
    if (valid_2 == false) { return 91; }

    std.string::string long_pattern = std.string::create();
    usize index_3 = 0usize;
    while (index_3 < 16385usize) { std.string::append_str(&long_pattern, "a"); index_3 += 1usize; }
    str long_text = long_pattern;
    bool rejected = false;
    try { std.regex::regex expression = std.regex::compile(long_text);
    test_observe(&expression); }
    catch (std.regex::error failure) {
        rejected = failure.code == std.regex::error_code::program_limit && failure.offset == 16384usize;
    }
    if (rejected == false) { return 92; }
    std.string::string ranges = std.string::from_str("[");
    usize index_4 = 0usize;
    while (index_4 < 4097usize) { std.string::append_str(&ranges, "a"); index_4 += 1usize; }
    std.string::append_str(&ranges, "]");
    str ranges_text = ranges;
    bool valid_3 = invalid(ranges_text, std.regex::error_code::program_limit);
    if (valid_3 == false) { return 93; }
    bool rejected_2 = false;
    try { std.regex::regex expression = std.regex::compile("a(");
    test_observe(&expression); }
    catch (std.regex::error failure) {
        rejected_2 = failure.code == std.regex::error_code::invalid_pattern && failure.offset == 1usize;
    }
    if (rejected_2 == false) { return 94; }

    std.regex::regex anchored = std.regex::compile("^a");
    TestStorage1 storage_later = {.value = std.regex::find_from(&anchored, "ba", 1usize)};
    switch (storage_later.value) {
    case variant o::some(value): return 95;
    case variant o::none: break;
    }
    bool rejected_3 = false;
    try { storage_later.value = std.regex::find_from(&anchored, "a", 2usize); }
    catch (std.regex::error failure) {
        rejected_3 = failure.code == std.regex::error_code::invalid_offset && failure.offset == 2usize;
    }
    if (rejected_3 == false) { return 96; }
    std.regex::regex adjacent = std.regex::compile("a*");
    array<std.regex::span> matches = std.regex::find_all(&adjacent, "a");
    if (len(matches) != 2usize) { return 97; }
    usize index_5 = 0usize;
    for (const std.regex::span* match in &matches) {
        if (match->start != index_5 || match->end != 1usize) { return 98; }
        index_5 += 1usize;
    }
    std.string::string replacement = std.regex::replace_all(&adjacent, "a", "#");
    str replacement_text = replacement;
    bool valid_4 = std.text::equal_ignore_ascii_case(replacement_text, "##");
    if (valid_4 == false) { return 99; }
    array<std.string::string> pieces = std.regex::split(&adjacent, "a");
    if (len(pieces) != 3usize) { return 100; }
    for (const std.string::string* piece in &pieces) {
        str text = *piece;
        if (len(text) != 0usize) { return 101; }
    }
    array<std.string::string> unchanged = std.regex::split(&anchored, "b");
    if (len(unchanged) != 1usize) { return 102; }
    for (const std.string::string* piece in &unchanged) {
        str text = *piece;
        valid_4 = std.text::equal_ignore_ascii_case(text, "b");
        if (valid_4 == false) { return 103; }
    }
    bool valid_5 = invalid("[\\d-a]", std.regex::error_code::invalid_pattern);
    if (valid_5 == false) { return 106; }
    bool valid_6 = invalid("[a-b-c]", std.regex::error_code::invalid_pattern);
    if (valid_6 == false) { return 107; }
    bool valid_7 = expect("[\\d-]+", "1-2", 0, 3, true);
    if (valid_7 == false) { return 108; }
    bool valid_8 = expect("[a\\-c]+", "a-c", 0, 3, true);
    if (valid_8 == false) { return 109; }
    std.regex::options settings = std.regex::default_options();
    settings.max_steps = 5usize;
    std.regex::regex budget = std.regex::compile_with_options("", settings);
    bool valid_9 = std.regex::is_match(&budget, "abc");
    if (valid_9 == false) { return 104; }
    bool rejected_4 = false;
    try { array<std.regex::span> values = std.regex::find_all(&budget, "abc");
    test_observe(&values); }
    catch (std.regex::error failure) { rejected_4 = failure.code == std.regex::error_code::step_limit; }
    if (rejected_4 == false) { return 105; }
    return 0;
}

i32 run() {
    try {
        bool valid = true;
        bool valid_10 = expect("", "", 0, 0, true);
        if (valid_10 == false) { return 1; }
        bool valid_11 = expect("", "ab", 0, 0, false);
        if (valid_11 == false) { return 2; }
        bool valid_12 = expect("a", "ba", 1, 2, false);
        if (valid_12 == false) { return 3; }
        bool valid_13 = expect("a", "b", -1, -1, false);
        if (valid_13 == false) { return 4; }
        bool valid_14 = expect("a|ab", "zabx", 1, 3, false);
        if (valid_14 == false) { return 5; }
        bool valid_15 = expect("(ab|a)b", "ab", 0, 2, true);
        if (valid_15 == false) { return 6; }
        bool valid_16 = expect("a*", "ba", 0, 0, false);
        if (valid_16 == false) { return 7; }
        bool valid_17 = expect("a+", "baaa!", 1, 4, false);
        if (valid_17 == false) { return 8; }
        bool valid_18 = expect("a?b", "b", 0, 1, true);
        if (valid_18 == false) { return 9; }
        bool valid_19 = expect("(ab)*", "abab", 0, 4, true);
        if (valid_19 == false) { return 10; }
        bool valid_20 = expect("(?:a|b)+", "abba", 0, 4, true);
        if (valid_20 == false) { return 11; }
        bool valid_21 = expect("(a?)*", "aaa", 0, 3, true);
        if (valid_21 == false) { return 12; }
        bool valid_22 = expect("(a*)*", "", 0, 0, true);
        if (valid_22 == false) { return 13; }
        bool valid_23 = expect("a|", "b", 0, 0, false);
        if (valid_23 == false) { return 14; }
        bool valid_24 = expect("|a", "a", 0, 1, true);
        if (valid_24 == false) { return 15; }
        bool valid_25 = expect("()", "", 0, 0, true);
        if (valid_25 == false) { return 16; }
        bool valid_26 = expect("a{0}", "a", 0, 0, false);
        if (valid_26 == false) { return 17; }
        bool valid_27 = expect("a{0,2}", "aaa", 0, 2, false);
        if (valid_27 == false) { return 18; }
        bool valid_28 = expect("a{2,}", "aaaa", 0, 4, true);
        if (valid_28 == false) { return 19; }
        bool valid_29 = expect("(ab){2,3}", "ababab", 0, 6, true);
        if (valid_29 == false) { return 20; }
        bool valid_30 = expect("((ab){1,2}){1,2}", "abababab", 0, 8, true);
        if (valid_30 == false) { return 21; }
        bool valid_31 = expect("(a|bc){2}", "bca", 0, 3, true);
        if (valid_31 == false) { return 22; }
        bool valid_32 = expect("(a?){2,3}", "aa", 0, 2, true);
        if (valid_32 == false) { return 23; }
        bool valid_33 = expect("^a$", "a", 0, 1, true);
        if (valid_33 == false) { return 24; }
        bool valid_34 = expect("^a$", "a\n", -1, -1, false);
        if (valid_34 == false) { return 25; }
        bool valid_35 = expect("$", "a", 1, 1, false);
        if (valid_35 == false) { return 26; }
        bool valid_36 = expect(".", "\n", -1, -1, false);
        if (valid_36 == false) { return 27; }
        bool valid_37 = expect(".", "é", 0, 2, true);
        if (valid_37 == false) { return 28; }
        bool valid_38 = expect("..", "é🙂", 0, 6, true);
        if (valid_38 == false) { return 29; }
        bool valid_39 = expect("[é-ê]+", "!éê!", 1, 5, false);
        if (valid_39 == false) { return 30; }
        bool valid_40 = expect("[^a]+", "aé🙂a", 1, 7, false);
        if (valid_40 == false) { return 31; }
        bool valid_41 = expect("[a-z]+", "zebra", 0, 5, true);
        if (valid_41 == false) { return 32; }
        bool valid_42 = expect("[-a]+", "-aa", 0, 3, true);
        if (valid_42 == false) { return 33; }
        bool valid_43 = expect("[a-]+", "a--", 0, 3, true);
        if (valid_43 == false) { return 34; }
        bool valid_44 = expect("\\d+", "a123b", 1, 4, false);
        if (valid_44 == false) { return 35; }
        bool valid_45 = expect("\\D+", "123abc", 3, 6, false);
        if (valid_45 == false) { return 36; }
        bool valid_46 = expect("\\w+", "a_1!", 0, 3, false);
        if (valid_46 == false) { return 37; }
        bool valid_47 = expect("\\W+", "é!", 0, 3, true);
        if (valid_47 == false) { return 38; }
        bool valid_48 = expect("\\s+", " \t\r\n", 0, 4, true);
        if (valid_48 == false) { return 39; }
        bool valid_49 = expect("\\S+", " a!", 1, 3, false);
        if (valid_49 == false) { return 40; }
        bool valid_50 = expect("\\bword\\b", "!word!", 1, 5, false);
        if (valid_50 == false) { return 41; }
        bool valid_51 = expect("\\Boo\\B", "food", 1, 3, false);
        if (valid_51 == false) { return 42; }
        bool valid_52 = expect("[\\dA-F]+", "A012F", 0, 5, true);
        if (valid_52 == false) { return 43; }
        bool valid_53 = expect("\\x00", "\0", 0, 1, true);
        if (valid_53 == false) { return 44; }
        bool valid_54 = expect("\\xE9", "é", 0, 2, true);
        if (valid_54 == false) { return 45; }
        bool valid_55 = expect("\\[a\\]", "[a]", 0, 3, true);
        if (valid_55 == false) { return 46; }
        bool valid_56 = expect("\\n", "\n", 0, 1, true);
        if (valid_56 == false) { return 47; }
        bool valid_57 = expect("(a{0}){2}b", "b", 0, 1, true);
        if (valid_57 == false) { return 48; }
        bool valid_58 = expect("(a|){0,2}b", "aab", 0, 3, true);
        if (valid_58 == false) { return 49; }
        bool valid_59 = invalid("[z-a]", std.regex::error_code::invalid_pattern);
        if (valid_59 == false) { return 61; }
        bool valid_60 = invalid("(", std.regex::error_code::invalid_pattern);
        if (valid_60 == false) { return 62; }
        bool valid_61 = invalid("a{3,2}", std.regex::error_code::invalid_pattern);
        if (valid_61 == false) { return 63; }
        bool valid_62 = invalid("a{1001}", std.regex::error_code::program_limit);
        if (valid_62 == false) { return 64; }
        bool valid_63 = invalid("a{1000}{1000}", std.regex::error_code::invalid_pattern);
        if (valid_63 == false) { return 65; }
        bool valid_64 = invalid("(a{1000}){1000}", std.regex::error_code::program_limit);
        if (valid_64 == false) { return 66; }
        bool valid_65 = invalid("(?=a)", std.regex::error_code::unsupported);
        if (valid_65 == false) { return 67; }
        bool valid_66 = invalid("(a)\\1", std.regex::error_code::unsupported);
        if (valid_66 == false) { return 68; }
        bool valid_67 = invalid("a*?", std.regex::error_code::unsupported);
        if (valid_67 == false) { return 69; }
        bool valid_68 = invalid("a++", std.regex::error_code::unsupported);
        if (valid_68 == false) { return 70; }
        bool valid_69 = invalid("\\p{L}", std.regex::error_code::unsupported);
        if (valid_69 == false) { return 71; }

        std.regex::options settings = std.regex::default_options();
        settings.ignore_ascii_case = true;
        std.regex::regex folded = std.regex::compile_with_options("[a-z]+", settings);
        bool valid_70 = std.regex::full_match(&folded, "HeLLo");
        if (valid_70 == false) { return 72; }
        std.regex::regex unicode_exact = std.regex::compile_with_options("é", settings);
        bool valid_71 = std.regex::is_match(&unicode_exact, "É");
        if (valid_71 == true) { return 73; }
        settings.multiline = true;
        settings.dot_all = true;
        std.regex::regex lines = std.regex::compile_with_options("^a.b$", settings);
        bool valid_72 = std.regex::is_match(&lines, "x\na\nb\nx");
        if (valid_72 == false) { return 74; }
        std.regex::regex empty = std.regex::compile("");
        array<std.regex::span> positions = std.regex::find_all(&empty, "é🙂");
        if (len(positions) != 3usize) { return 75; }
        usize index = 0usize;
        for (const std.regex::span* position in &positions) {
            usize expected = index == 0usize ? 0usize : (index == 1usize ? 2usize : 6usize);
            if (position->start != expected || position->end != expected) { return 76; }
            index += 1usize;
        }
        std.string::string inserted = std.regex::replace_all(&empty, "é", "$1");
        str inserted_text = inserted;
        bool valid_73 = std.text::equal_ignore_ascii_case(inserted_text, "$1é$1");
        if (valid_73 == false) { return 77; }
        std.regex::regex separators = std.regex::compile(",+");
        array<std.string::string> pieces = std.regex::split(&separators, ",one,,two,");
        if (len(pieces) != 4usize) { return 78; }
        usize index_6 = 0usize;
        for (const std.string::string* part in &pieces) {
            str actual = *part;
            constexpr str expected = index_6 == 1usize ? "one" : (index_6 == 2usize ? "two" : "");
            valid_73 = std.text::equal_ignore_ascii_case(actual, expected);
            if (valid_73 == false) { return 79; }
            index_6 += 1usize;
        }
        std.string::string escaped = std.regex::escape_literal("a+b[0].🙂");
        str escaped_text = escaped;
        std.regex::regex literal = std.regex::compile(escaped_text);
        drop escaped;
        TestStorage2 storage_valid_73_2 = {.value = std.regex::full_match(&literal, "a+b[0].🙂")};
        if (storage_valid_73_2.value == false) { return 80; }
        o<std.regex::span> later = std.regex::find_from(&literal, "a+b[0].🙂 a+b[0].🙂", 1usize);
        switch (later) {
        case variant o::some(value):
            if (value->start != 12usize || value->end != 23usize) { return 81; }
            break;
        case variant o::none: return 82;
        }
        bool rejected = false;
        try {
            o<std.regex::span> bad = std.regex::find_from(&empty, "é", 1usize);
            bad as void;
        } catch (std.regex::error failure) {
            rejected = failure.code == std.regex::error_code::invalid_offset;
        }
        if (rejected == false) { return 83; }
        settings.max_steps = 1usize;
        std.regex::regex limited = std.regex::compile_with_options("a+", settings);
        bool rejected_5 = false;
        try { storage_valid_73_2.value = std.regex::is_match(&limited, "aaaa"); }
        catch (std.regex::error failure) { rejected_5 = failure.code == std.regex::error_code::step_limit; }
        if (rejected_5 == false) { return 84; }
        settings.max_steps = 0usize;
        bool rejected_6 = false;
        try { std.regex::regex bad = std.regex::compile_with_options("", settings);
        test_observe(&bad); }
        catch (std.regex::error failure) { rejected_6 = failure.code == std.regex::error_code::invalid_options; }
        if (rejected_6 == false) { return 85; }
        i32 edges = edge_cases();
        if (edges != 0) { return edges; }
    } catch (std.regex::error failure) {
        return 200;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 210;
    }
    return 0;
}
