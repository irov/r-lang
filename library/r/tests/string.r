module tests.std.string;
import std.test;
import std.string;

// The tests of the R part of std.string (Library R-SLIB-STRING-0004): replacing a range and
// inserting text between scalar boundaries of an owned string; every failure reports its
// boundary_error and leaves the string unchanged. Run in test mode (Core R-FUNC-0025).

@test
void inserts_at_scalar_boundaries()
    throws std.string::boundary_error, std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.string::from_str("héllo");
    std.string::insert_str(&text, 0usize, ">");
    std.test::equal_text(text, ">héllo");
    std.string::insert_str(&text, 2usize, "[");
    std.string::insert_str(&text, 5usize, "]");
    std.test::equal_text(text, ">h[é]llo");
    usize end = std.string::len(&text);
    std.string::insert_str(&text, end, " €😀");
    std.test::equal_text(text, ">h[é]llo €😀");
    std.string::insert_str(&text, 3usize, "");
    std.test::equal_text(text, ">h[é]llo €😀");
    std.test::equal(std.string::len(&text), 17usize);
}

@test
void replaces_ranges()
    throws std.string::boundary_error, std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.string::from_str("hello world");
    std.string::replace_range(&text, 6usize, 11usize, "there");
    std.test::equal_text(text, "hello there");
    std.string::replace_range(&text, 0usize, 5usize, "hi");
    std.test::equal_text(text, "hi there");
    std.string::replace_range(&text, 2usize, 3usize, "");
    std.test::equal_text(text, "hithere");
    std.string::replace_range(&text, 2usize, 2usize, ", ");
    std.test::equal_text(text, "hi, there");
    usize end = std.string::len(&text);
    std.string::replace_range(&text, 0usize, end, "");
    std.test::equal_text(text, "");
    std.test::equal(std.string::len(&text), 0usize);
    std.string::replace_range(&text, 0usize, 0usize, "again");
    std.test::equal_text(text, "again");
}

@test
void replaces_multibyte_scalars()
    throws std.string::boundary_error, std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.string::from_str("aéb€c😀d");
    std.string::replace_range(&text, 1usize, 3usize, "e");
    std.test::equal_text(text, "aeb€c😀d");
    std.string::replace_range(&text, 3usize, 6usize, "€€");
    std.test::equal_text(text, "aeb€€c😀d");
    std.string::replace_range(&text, 10usize, 14usize, "!");
    std.test::equal_text(text, "aeb€€c!d");
    std.test::equal(std.string::len(&text), 12usize);
}

@test
void rejects_ranges_out_of_bounds() throws std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.string::from_str("hello");
    try {
        std.string::replace_range(&text, 1usize, 9usize, "x");
        std.test::fail("an end beyond the length");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::out_of_bounds, "end beyond");
    }
    try {
        std.string::replace_range(&text, 3usize, 2usize, "x");
        std.test::fail("a start after the end");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::out_of_bounds, "start after end");
    }
    try {
        std.string::insert_str(&text, 6usize, "x");
        std.test::fail("an index beyond the length");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::out_of_bounds, "index beyond");
    }
    std.test::equal_text(text, "hello");
    std.test::equal(std.string::len(&text), 5usize);
}

@test
void rejects_indices_inside_scalars() throws std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.string::from_str("aé€😀");
    try {
        std.string::insert_str(&text, 2usize, "x");
        std.test::fail("inside a two-byte scalar");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::not_scalar_boundary, "in é");
    }
    try {
        std.string::replace_range(&text, 0usize, 4usize, "x");
        std.test::fail("an end inside a three-byte scalar");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::not_scalar_boundary, "in €");
    }
    try {
        std.string::replace_range(&text, 8usize, 10usize, "x");
        std.test::fail("a start inside a four-byte scalar");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::not_scalar_boundary, "in 😀");
    }
    try {
        std.string::replace_range(&text, 3usize, 12usize, "x");
        std.test::fail("an end beyond the length before the scalar check");
    } catch (std.string::boundary_error failure) {
        std.test::check(failure == std.string::boundary_error::out_of_bounds,
                        "bounds come before boundaries");
    }
    std.test::equal_text(text, "aé€😀");
}

@test(expect = std.string::boundary_error)
void inserting_into_an_empty_string_past_its_end() throws std.string::boundary_error,
    std.alloc::alloc_error {
    std.string::string text = std.string::create();
    std.string::insert_str(&text, 1usize, "x");
}

@test(allocations)
void builds_text_by_edits()
    throws std.string::boundary_error, std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.string::create();
    std.string::insert_str(&text, 0usize, "world");
    std.string::insert_str(&text, 0usize, "hello ");
    std.string::replace_range(&text, 0usize, 1usize, "H");
    usize end = std.string::len(&text);
    std.string::insert_str(&text, end, "!");
    std.string::replace_range(&text, 5usize, 6usize, ", ");
    std.test::equal_text(text, "Hello, world!");
    std.string::replace_range(&text, 7usize, 12usize, "wörld");
    std.test::equal_text(text, "Hello, wörld!");
    std.test::equal(std.string::len(&text), 14usize);
}
