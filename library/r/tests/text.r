module tests.std.text;
import std.test;
import std.text;

// The tests of std.text (Library R-SLIB-TEXT-0001..0004), run in test mode (Core R-FUNC-0025).

@test
void searches_bytes() throws std.test::failure, std.alloc::alloc_error {
    std.test::check(std.text::starts_with("hello world", "hello"), "starts_with");
    std.test::check(std.text::ends_with("hello world", "world"), "ends_with");
    std.test::check(std.text::contains("hello world", "o w"), "contains");
    std.test::check(std.text::contains("hello", "") == true, "the empty needle occurs");
    std.test::check(std.text::starts_with("he", "hello") == false, "a longer prefix");
    std.test::equal(std.text::count_byte("banana", 97u8), 3usize);
    std.test::check(std.text::is_ascii("plain"), "ASCII text");
    std.test::check(std.text::is_ascii("naïve") == false, "non-ASCII text");
    std.test::equal(std.text::to_ascii_lower(65u8), 97u8);
    std.test::equal(std.text::to_ascii_upper(122u8), 90u8);
    std.test::check(std.text::equal_ignore_ascii_case("MiXeD", "mixed"), "ASCII case folding");
    std.test::check(std.text::equal_ignore_ascii_case("É", "é") == false,
                    "only ASCII letters fold");
}

@test
void finds_the_first_occurrence() throws std.test::failure, std.alloc::alloc_error {
    o<usize> found = std.text::find("banana", "nan");
    switch (found) {
    case variant o::some(index): std.test::equal(*index, 2usize);
    case variant o::none: std.test::fail("nan is in banana");
    }
    o<usize> missing = std.text::find("banana", "xyz");
    switch (missing) {
    case variant o::some(index): std.test::fail("xyz is not in banana");
    case variant o::none: break;
    }
}

@test
void trims_ascii_whitespace() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal_text(std.text::trim(" \t\r\n a b \n\t"), "a b");
    std.test::equal_text(std.text::trim_start("  x  "), "x  ");
    std.test::equal_text(std.text::trim_end("  x  "), "  x");
    std.test::equal_text(std.text::trim(" \n "), "");
}

@test
void walks_scalars() throws std.test::failure, std.alloc::alloc_error {
    str accented = "aé€😀";
    std.test::check(std.text::is_scalar_boundary(accented, 2usize) == false, "inside é");
    std.test::equal(std.text::next_scalar_boundary(accented, 1usize), 3usize);
    std.test::equal(std.text::previous_scalar_boundary(accented, 10usize), 6usize);
    usize count = 0usize;
    u32 total = 0u32;
    for (char value in std.text::scalars(accented)) {
        count += 1usize;
        total += value as u32;
    }
    std.test::equal(count, 4usize);
    std.test::equal(total, 97u32 + 233u32 + 8364u32 + 128512u32);
}

@test
void splits_pieces_and_lines() throws std.test::failure, std.alloc::alloc_error {
    std.string::string joined = std.string::create();
    for (str piece in std.text::split("a,bb,,c", ",")) {
        joined.append("[");
        joined.append(piece);
        joined.append("]");
    }
    std.test::equal_text(joined, "[a][bb][][c]");
    std.string::string rows = std.string::create();
    for (str line in std.text::lines("one\r\ntwo\n\nthree\n")) {
        rows.append(line);
        rows.append("|");
    }
    std.test::equal_text(rows, "one|two||three|");
}

@test(allocations)
void builds_new_text() throws std.test::failure, std.alloc::alloc_error {
    str[3] parts = {"a", "b", "c"};
    std.string::string joined = std.text::join(parts[0usize..3usize], ", ");
    std.test::equal_text(joined, "a, b, c");
    std.string::string replaced = std.text::replace("a-b-c", "-", "+");
    std.test::equal_text(replaced, "a+b+c");
    std.string::string upper = std.text::ascii_uppercase("MixEd é");
    std.test::equal_text(upper, "MIXED é");
    std.string::string lower = std.text::ascii_lowercase("MixEd É");
    std.test::equal_text(lower, "mixed É");
}
