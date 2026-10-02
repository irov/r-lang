module test.codegen.regex_captures;

import std.regex;

// R-SLIB-REGEX-0007 (M19): capturing groups of the match that find selects — the preferred path
// among those matching exactly that span, the last iteration of a repeated group, none for a
// group the path does not enter — group numbering, offsets, and the shared step budget.

// 0 when group index of groups spans start..end, 100 + index when it is absent.
protected i32 group_at(const array<o<std.regex::span>>* groups, usize index, usize start,
                       usize end) {
    auto slot = std.array::get(groups, index);
    switch (slot) {
    case variant o::some(entry):
        switch (**entry) {
        case variant o::some(found):
            if (found->start != start || found->end != end) { return 1; }
            return 0;
        case variant o::none:
            return 2;
        }
    case variant o::none:
        return 3;
    }
    return 4;
}

protected i32 group_absent(const array<o<std.regex::span>>* groups, usize index) {
    auto slot = std.array::get(groups, index);
    switch (slot) {
    case variant o::some(entry):
        switch (**entry) {
        case variant o::some(found): return 1;
        case variant o::none: return 0;
        }
    case variant o::none:
        return 3;
    }
    return 4;
}

// Captures pattern in text; 0 when the groups equal the expected starts and ends, where a start
// of 99 marks an absent group.
protected i32 expect(str pattern, str text, const usize[] starts, const usize[] ends)
    throws std.regex::error, std.alloc::alloc_error {
    std.regex::regex compiled = std.regex::compile(pattern);
    if (std.regex::group_count(&compiled) + 1usize != len(starts)) { return 10; }
    o<array<o<std.regex::span>>> found = std.regex::captures(&compiled, text);
    switch (found) {
    case variant o::some(groups):
        if (len(*groups) != len(starts)) { return 11; }
        for (usize index = 0usize; index < len(starts); index += 1usize) {
            if (starts[index] == 99usize) {
                if (group_absent(groups, index) != 0) { return 12; }
            } else {
                if (group_at(groups, index, starts[index], ends[index]) != 0) { return 13; }
            }
        }
        return 0;
    case variant o::none:
        return 14;
    }
}

protected i32 check() throws std.regex::error, std.alloc::alloc_error {
    usize[4] date_starts = {3usize, 3usize, 8usize, 99usize};
    usize[4] date_ends = {10usize, 7usize, 10usize, 0usize};
    if (expect("(\\d+)-(\\d+)(?:-(\\d+))?", "on 2026-09 ok", date_starts, date_ends) != 0) {
        return 1;
    }
    usize[2] last_starts = {2usize, 5usize};
    usize[2] last_ends = {6usize, 6usize};
    if (expect("(a|b)+", "xxabba", last_starts, last_ends) != 0) { return 2; }
    usize[3] choice_starts = {0usize, 99usize, 0usize};
    usize[3] choice_ends = {2usize, 0usize, 2usize};
    if (expect("(a)|(ab)", "ab", choice_starts, choice_ends) != 0) { return 3; }
    usize[3] prefer_starts = {0usize, 0usize, 1usize};
    usize[3] prefer_ends = {2usize, 1usize, 2usize};
    if (expect("(a|ab)(b?)", "ab", prefer_starts, prefer_ends) != 0) { return 4; }
    usize[3] span_starts = {0usize, 0usize, 1usize};
    usize[3] span_ends = {4usize, 1usize, 4usize};
    if (expect("(a|ab)(c|bcd)", "abcd", span_starts, span_ends) != 0) { return 5; }
    usize[3] greedy_starts = {0usize, 0usize, 3usize};
    usize[3] greedy_ends = {3usize, 3usize, 3usize};
    if (expect("(a*)(a*)", "aaa", greedy_starts, greedy_ends) != 0) { return 6; }
    usize[3] nested_starts = {0usize, 2usize, 2usize};
    usize[3] nested_ends = {4usize, 4usize, 3usize};
    if (expect("((a)b)+", "abab", nested_starts, nested_ends) != 0) { return 7; }
    usize[2] empty_starts = {0usize, 1usize};
    usize[2] empty_ends = {2usize, 1usize};
    if (expect("x(y*)z", "xz", empty_starts, empty_ends) != 0) { return 8; }
    usize[2] scalar_starts = {1usize, 3usize};
    usize[2] scalar_ends = {6usize, 5usize};
    if (expect("(é)+\\w", "aééx", scalar_starts, scalar_ends) != 0) { return 9; }
    usize[1] plain_starts = {2usize};
    usize[1] plain_ends = {4usize};
    if (expect("c(?:d)", "abcd", plain_starts, plain_ends) != 0) { return 15; }

    std.regex::regex word = std.regex::compile("(\\w+)");
    o<array<o<std.regex::span>>> second = std.regex::captures_from(&word, "ab cd", 2usize);
    switch (second) {
    case variant o::some(groups):
        if (group_at(groups, 1usize, 3usize, 5usize) != 0) { return 16; }
    case variant o::none:
        return 17;
    }
    o<array<o<std.regex::span>>> none_left = std.regex::captures_from(&word, "ab  ", 2usize);
    switch (none_left) {
    case variant o::some(groups): return 18;
    case variant o::none: break;
    }
    try {
        std.regex::captures_from(&word, "é", 1usize) as void;
        return 19;
    } catch (std.regex::error failure) {
        if (failure.code != std.regex::error_code::invalid_offset) { return 20; }
    }
    std.regex::options tight = std.regex::default_options();
    tight.max_steps = 3usize;
    std.regex::regex limited = std.regex::compile_with_options("(a+)+b", tight);
    try {
        std.regex::captures(&limited, "aaaaaaaaab") as void;
        return 21;
    } catch (std.regex::error failure) {
        if (failure.code != std.regex::error_code::step_limit) { return 22; }
    }
    std.regex::options folded = std.regex::default_options();
    folded.ignore_ascii_case = true;
    std.regex::regex caseless = std.regex::compile_with_options("(AB)c", folded);
    o<array<o<std.regex::span>>> mixed = std.regex::captures(&caseless, "xabC");
    switch (mixed) {
    case variant o::some(groups):
        if (group_at(groups, 1usize, 1usize, 3usize) != 0) { return 23; }
    case variant o::none:
        return 24;
    }
    return 0;
}

i32 main() {
    try {
        return check();
    } catch (std.regex::error failure) {
        failure as void;
        return 98;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    }
}
