module tests.std.regex;
import std.test;
import std.regex;

// The tests of std.regex (Library R-SLIB-REGEX-0001..0007), run in test mode (Core R-FUNC-0025).

/* Fails unless the search found exactly the span start..end. */
protected void expect_span(o<std.regex::span> found, usize start, usize end)
    throws std.test::failure, std.alloc::alloc_error {
    switch (found) {
    case variant o::some(value):
        std.test::equal(value->start, start);
        std.test::equal(value->end, end);
    case variant o::none: std.test::fail("expected a match");
    }
}

/* Fails unless the search found nothing. */
protected void expect_none(o<std.regex::span> found) throws std.test::failure, std.alloc::alloc_error {
    switch (found) {
    case variant o::some(value): std.test::fail("expected no match");
    case variant o::none: break;
    }
}

/* The spans as `[start,end)` items, for comparison with one text. */
protected std.string::string listed(const array<std.regex::span>* spans)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (const std.regex::span* found in spans) {
        std.string::string item = f"[{found->start},{found->end})";
        text.append(item.as_str());
    }
    return move text;
}

/* The pieces as `[piece]` items. */
protected std.string::string bracketed(const array<std.string::string>* pieces)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (const std.string::string* piece in pieces) {
        text.append("[");
        text.append(piece->as_str());
        text.append("]");
    }
    return move text;
}

/* The span of group `index` of a capture result. */
protected o<std.regex::span> group(const array<o<std.regex::span>>* groups, usize index) {
    return (*groups)[index];
}

/* Fails unless compiling the pattern reports the code at the offset. */
protected void expect_compile_error(str pattern, std.regex::error_code code, usize offset)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        std.regex::regex compiled = std.regex::compile(pattern);
        drop compiled;
        std.string::string message = f"{pattern} compiled";
        std.test::fail(message.as_str());
    } catch (std.regex::error failure) {
        std.string::string message = f"the error code of {pattern}";
        std.test::check(failure.code == code, message.as_str());
        std.test::equal(failure.offset, offset);
    }
}

@test
void finds_the_leftmost_longest_match()
    throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    std.regex::regex number = std.regex::compile("[0-9]+");
    expect_span(std.regex::find(&number, "item=42 count=7"), 5usize, 7usize);
    expect_span(std.regex::find_from(&number, "item=42 count=7", 7usize), 14usize, 15usize);
    expect_none(std.regex::find(&number, "no digits"));
    std.test::check(std.regex::is_match(&number, "a1b") == true, "a digit occurs");
    std.test::check(std.regex::full_match(&number, "a1b") == false, "letters around the digit");
    std.test::check(std.regex::full_match(&number, "2026") == true, "only digits");
    // The longest match at the earliest position wins, whatever the order of the alternatives.
    std.regex::regex choice = std.regex::compile("a|ab");
    expect_span(std.regex::find(&choice, "zabx"), 1usize, 3usize);
    // find_from keeps the absolute anchor context of the subject.
    std.regex::regex anchored = std.regex::compile("^a");
    expect_none(std.regex::find_from(&anchored, "ba", 1usize));
    // Positions are UTF-8 byte offsets and dot takes one scalar.
    std.regex::regex two = std.regex::compile("..");
    expect_span(std.regex::find(&two, "é🙂"), 0usize, 6usize);
    std.regex::regex word = std.regex::compile("\\bword\\b");
    expect_span(std.regex::find(&word, "swords word!"), 7usize, 11usize);
    // Counted repetition and scalar escapes.
    std.regex::regex counted = std.regex::compile("a{2,3}");
    expect_span(std.regex::find(&counted, "baaaa"), 1usize, 4usize);
    std.regex::regex escapes = std.regex::compile("\\x41\\t[^a-c]");
    expect_span(std.regex::find(&escapes, "xA\tdy"), 1usize, 4usize);
}

@test
void finds_all_matches_in_order()
    throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    std.regex::regex digits = std.regex::compile("\\d+");
    array<std.regex::span> numbers = std.regex::find_all(&digits, "a1b22c333");
    std.string::string numbers_text = listed(&numbers);
    std.test::equal_text(numbers_text.as_str(), "[1,2)[3,5)[6,9)");
    // After an empty match the search advances one scalar; the empty match at the end counts.
    std.regex::regex empty = std.regex::compile("");
    array<std.regex::span> positions = std.regex::find_all(&empty, "é🙂");
    std.string::string positions_text = listed(&positions);
    std.test::equal_text(positions_text.as_str(), "[0,0)[2,2)[6,6)");
    // Empty matches next to a nonempty match are included.
    std.regex::regex stars = std.regex::compile("a*");
    array<std.regex::span> runs = std.regex::find_all(&stars, "baab");
    std.string::string runs_text = listed(&runs);
    std.test::equal_text(runs_text.as_str(), "[0,0)[1,3)[3,3)[4,4)");
    array<std.regex::span> nothing = std.regex::find_all(&digits, "none");
    std.test::equal(len(nothing), 0usize);
}

@test
void replaces_splits_and_escapes_text()
    throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    std.regex::regex number = std.regex::compile("[0-9]+");
    std.string::string redacted = std.regex::replace_all(&number, "item=42 count=7", "#");
    std.test::equal_text(redacted.as_str(), "item=# count=#");
    // The replacement is literal text: no dollar or backslash substitution.
    std.string::string literal = std.regex::replace_all(&number, "a1é2", "$0\\1");
    std.test::equal_text(literal.as_str(), "a$0\\1é$0\\1");
    std.regex::regex separator = std.regex::compile("[,;]\\s*");
    array<std.string::string> fields = std.regex::split(&separator, "one, two;three");
    std.string::string fields_text = bracketed(&fields);
    std.test::equal_text(fields_text.as_str(), "[one][two][three]");
    // Leading, trailing and adjacent empty pieces stay; no match leaves the whole text.
    std.regex::regex comma = std.regex::compile(",");
    array<std.string::string> pieces = std.regex::split(&comma, ",a,,b,");
    std.string::string pieces_text = bracketed(&pieces);
    std.test::equal_text(pieces_text.as_str(), "[][a][][b][]");
    array<std.string::string> whole = std.regex::split(&comma, "abc");
    std.string::string whole_text = bracketed(&whole);
    std.test::equal_text(whole_text.as_str(), "[abc]");
    // An escaped text compiles to a pattern that matches exactly that text.
    std.string::string escaped = std.regex::escape_literal("a+b[0].(x)?é");
    std.test::equal_text(escaped.as_str(), "a\\+b\\[0\\]\\.\\(x\\)\\?é");
    std.regex::regex exact = std.regex::compile(escaped.as_str());
    std.test::check(std.regex::full_match(&exact, "a+b[0].(x)?é") == true,
                    "the escaped pattern matches its text");
    std.test::check(std.regex::is_match(&exact, "aab0x") == false,
                    "metacharacters lose their meaning");
}

@test
void applies_the_options() throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    std.regex::options defaults = std.regex::default_options();
    std.test::check(defaults.ignore_ascii_case == false, "case folding is off");
    std.test::check(defaults.multiline == false, "multiline is off");
    std.test::check(defaults.dot_all == false, "dot_all is off");
    std.test::equal(defaults.max_steps, 10000000usize);
    std.regex::options folded = std.regex::default_options();
    folded.ignore_ascii_case = true;
    std.regex::regex letters = std.regex::compile_with_options("[a-z]+", folded);
    std.test::check(std.regex::full_match(&letters, "HeLLo") == true, "ASCII letters fold");
    std.regex::regex accented = std.regex::compile_with_options("é", folded);
    std.test::check(std.regex::is_match(&accented, "É") == false, "only ASCII letters fold");
    std.regex::regex strict = std.regex::compile("^a.b$");
    std.test::check(std.regex::is_match(&strict, "x\na\nb\nx") == false,
                    "anchors are absolute and dot excludes LF");
    std.regex::options lines = std.regex::default_options();
    lines.multiline = true;
    lines.dot_all = true;
    std.regex::regex relaxed = std.regex::compile_with_options("^a.b$", lines);
    std.test::check(std.regex::is_match(&relaxed, "x\na\nb\nx") == true,
                    "multiline anchors and dot_all");
}

@test
void captures_groups() throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    std.regex::regex date = std.regex::compile("(\\d+)-(\\d+)(?:-(\\d+))?");
    std.test::equal(std.regex::group_count(&date), 3usize);
    o<array<o<std.regex::span>>> found = std.regex::captures(&date, "on 2026-09 ok");
    switch (found) {
    case variant o::some(groups):
        std.test::equal(len(*groups), 4usize);
        expect_span(group(groups, 0usize), 3usize, 10usize);
        expect_span(group(groups, 1usize), 3usize, 7usize);
        expect_span(group(groups, 2usize), 8usize, 10usize);
        expect_none(group(groups, 3usize));
    case variant o::none: std.test::fail("a date occurs");
    }
    // A repeated group reports its last iteration.
    std.regex::regex repeated = std.regex::compile("(a|b)+");
    o<array<o<std.regex::span>>> last = std.regex::captures(&repeated, "xxabba");
    switch (last) {
    case variant o::some(groups): expect_span(group(groups, 1usize), 5usize, 6usize);
    case variant o::none: std.test::fail("a run of a and b occurs");
    }
    std.regex::regex word = std.regex::compile("(\\w+)");
    o<array<o<std.regex::span>>> second = std.regex::captures_from(&word, "ab cd", 2usize);
    switch (second) {
    case variant o::some(groups): expect_span(group(groups, 1usize), 3usize, 5usize);
    case variant o::none: std.test::fail("cd follows offset 2");
    }
    o<array<o<std.regex::span>>> missing = std.regex::captures(&word, "!?");
    switch (missing) {
    case variant o::some(groups): std.test::fail("no word occurs");
    case variant o::none: break;
    }
}

@test(expect = std.regex::error)
void rejects_an_unclosed_group() throws std.regex::error, std.alloc::alloc_error {
    std.regex::regex compiled = std.regex::compile("a(b");
    drop compiled;
}

@test
void reports_error_codes_and_offsets()
    throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    expect_compile_error("a(", std.regex::error_code::invalid_pattern, 1usize);
    expect_compile_error(")", std.regex::error_code::invalid_pattern, 0usize);
    expect_compile_error("*a", std.regex::error_code::invalid_pattern, 0usize);
    expect_compile_error("[z-a]", std.regex::error_code::invalid_pattern, 0usize);
    expect_compile_error("a{3,2}", std.regex::error_code::invalid_pattern, 1usize);
    expect_compile_error("(?=a)", std.regex::error_code::unsupported, 0usize);
    expect_compile_error("a*?", std.regex::error_code::unsupported, 2usize);
    expect_compile_error("\\p{L}", std.regex::error_code::unsupported, 0usize);
    expect_compile_error("a{1001}", std.regex::error_code::program_limit, 2usize);
    std.regex::options zero = std.regex::default_options();
    zero.max_steps = 0usize;
    try {
        std.regex::regex compiled = std.regex::compile_with_options("a", zero);
        drop compiled;
        std.test::fail("zero max_steps compiled");
    } catch (std.regex::error failure) {
        std.test::check(failure.code == std.regex::error_code::invalid_options, "invalid options");
        std.test::equal(failure.offset, 0usize);
    }
    // Search errors: offsets inside a scalar or beyond the subject, and the step budget.
    std.regex::regex any = std.regex::compile("a");
    try {
        o<std.regex::span> found = std.regex::find_from(&any, "é", 1usize);
        found as void;
        std.test::fail("an offset inside a scalar");
    } catch (std.regex::error failure) {
        std.test::check(failure.code == std.regex::error_code::invalid_offset, "inside a scalar");
        std.test::equal(failure.offset, 1usize);
    }
    try {
        o<std.regex::span> found = std.regex::find_from(&any, "a", 2usize);
        found as void;
        std.test::fail("an offset beyond the subject");
    } catch (std.regex::error failure) {
        std.test::check(failure.code == std.regex::error_code::invalid_offset, "beyond the end");
        std.test::equal(failure.offset, 2usize);
    }
    std.regex::options tight = std.regex::default_options();
    tight.max_steps = 1usize;
    std.regex::regex limited = std.regex::compile_with_options("a+", tight);
    try {
        bool matched = std.regex::is_match(&limited, "aaaa");
        matched as void;
        std.test::fail("one step is not enough");
    } catch (std.regex::error failure) {
        std.test::check(failure.code == std.regex::error_code::step_limit, "the step limit");
    }
}

@test(allocations)
void compiles_and_rewrites_under_allocation_failures()
    throws std.test::failure, std.alloc::alloc_error, std.regex::error {
    std.regex::regex separator = std.regex::compile("[,;] *");
    array<std.string::string> fields = std.regex::split(&separator, "a, b;c");
    std.string::string fields_text = bracketed(&fields);
    std.test::equal_text(fields_text.as_str(), "[a][b][c]");
    std.string::string joined = std.regex::replace_all(&separator, "a, b;c", "+");
    std.test::equal_text(joined.as_str(), "a+b+c");
}
