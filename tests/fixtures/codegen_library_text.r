module test.codegen.library_text;

import std.text;

// R-SLIB-TEXT-0002..0004 (M19): trimming, scalar boundaries and scalars, splitting into pieces
// and lines, and the allocating join, replace and ASCII case folding.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected i32 check_views() {
    if (same(std.text::trim(" \t\r\n a b \n\t"), "a b") == false) { return 1; }
    if (same(std.text::trim_start("  x  "), "x  ") == false) { return 2; }
    if (same(std.text::trim_end("  x  "), "  x") == false) { return 3; }
    if (same(std.text::trim(" \n "), "") == false) { return 4; }
    if (std.text::to_ascii_upper(97u8) != 65u8 || std.text::to_ascii_upper(90u8) != 90u8) {
        return 5;
    }
    if (std.text::to_ascii_upper(123u8) != 123u8) { return 6; }
    str accented = "aé€😀";
    if (std.text::is_scalar_boundary(accented, 0usize) == false) { return 7; }
    if (std.text::is_scalar_boundary(accented, 2usize) == true) { return 8; }
    if (std.text::is_scalar_boundary(accented, 10usize) == false) { return 9; }
    if (std.text::is_scalar_boundary(accented, 11usize) == true) { return 10; }
    if (std.text::next_scalar_boundary(accented, 1usize) != 3usize) { return 11; }
    if (std.text::next_scalar_boundary(accented, 3usize) != 6usize) { return 12; }
    if (std.text::next_scalar_boundary(accented, 10usize) != 10usize) { return 13; }
    if (std.text::previous_scalar_boundary(accented, 10usize) != 6usize) { return 14; }
    if (std.text::previous_scalar_boundary(accented, 2usize) != 1usize) { return 15; }
    if (std.text::previous_scalar_boundary(accented, 0usize) != 0usize) { return 16; }
    u32 total = 0u32;
    usize count = 0usize;
    for (char value in std.text::scalars(accented)) {
        total += value as u32;
        count += 1usize;
    }
    if (count != 4usize || total != 97u32 + 233u32 + 8364u32 + 128512u32) { return 17; }
    return 0;
}

protected i32 check_pieces() {
    usize pieces = 0usize;
    for (str piece in std.text::split("a,bb,,c", ",")) {
        pieces += 1usize;
        if (pieces == 2usize && same(piece, "bb") == false) { return 20; }
        if (pieces == 3usize && same(piece, "") == false) { return 21; }
    }
    if (pieces != 4usize) { return 22; }
    usize wide = 0usize;
    for (str piece in std.text::split("x--y--", "--")) {
        wide += 1usize;
        if (wide == 3usize && same(piece, "") == false) { return 23; }
    }
    if (wide != 3usize) { return 24; }
    usize whole = 0usize;
    for (str piece in std.text::split("abc", "")) {
        whole += 1usize;
        if (same(piece, "abc") == false) { return 25; }
    }
    if (whole != 1usize) { return 26; }
    usize empty = 0usize;
    for (str piece in std.text::split("", ";")) {
        empty += 1usize;
        if (same(piece, "") == false) { return 27; }
    }
    if (empty != 1usize) { return 28; }
    usize rows = 0usize;
    for (str line in std.text::lines("one\r\ntwo\n\nthree\n")) {
        rows += 1usize;
        if (rows == 1usize && same(line, "one") == false) { return 29; }
        if (rows == 3usize && same(line, "") == false) { return 30; }
        if (rows == 4usize && same(line, "three") == false) { return 31; }
    }
    if (rows != 4usize) { return 32; }
    usize none = 0usize;
    for (str line in std.text::lines("")) { none += 1usize; }
    if (none != 0usize) { return 33; }
    std.text::lines_iter single = std.text::lines("last");
    o<str> first = single.next();
    switch (first) {
    case variant o::some(line):
        if (same(*line, "last") == false) { return 34; }
    case variant o::none:
        return 35;
    }
    return 0;
}

protected i32 check_owned() throws std.alloc::alloc_error {
    str[3] words = {"x", "", "z"};
    std.string::string joined = std.text::join(words, "--");
    if (same(joined, "x----z") == false) { return 40; }
    str[1] lone = {"only"};
    std.string::string single = std.text::join(lone, ",");
    if (same(single, "only") == false) { return 41; }
    const str[] nothing = words[0usize..0usize];
    std.string::string blank = std.text::join(nothing, ",");
    if (same(blank, "") == false) { return 42; }
    std.string::string replaced = std.text::replace("aaa", "aa", "b");
    if (same(replaced, "ba") == false) { return 43; }
    std.string::string kept = std.text::replace("abc", "", "-");
    if (same(kept, "abc") == false) { return 44; }
    std.string::string grown = std.text::replace("é-é", "é", "ee");
    if (same(grown, "ee-ee") == false) { return 45; }
    std.string::string upper = std.text::ascii_uppercase("abc é z");
    if (same(upper, "ABC é Z") == false) { return 46; }
    std.string::string lower = std.text::ascii_lowercase("ABC É Z");
    if (same(lower, "abc É z") == false) { return 47; }
    return 0;
}

i32 main() {
    try {
        i32 views = check_views();
        if (views != 0) { return views; }
        i32 pieces = check_pieces();
        if (pieces != 0) { return pieces; }
        return check_owned();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    }
}
