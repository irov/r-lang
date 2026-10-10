module tests.std.dl;
import std.test;
import std.dl;

// The tests of std.dl (Library R-SLIB-DL-0001..0004): a library opened by path and by name, the
// addresses of its symbols, a symbol it does not define, refused paths, and a function of a
// loadable block obtained with its verified prototype. Run in test mode (Core R-FUNC-0025).

@link(name = "system.loader", kind = "loadable")
@header("stdlib.h")
extern "C" {
    @safety("LOADER-ABS", "value is not the most negative c_int")
    c_int abs(c_int value);
}

@test
void opens_by_path_and_by_name() throws std.test::failure, std.dl::dl_error, std.alloc::alloc_error {
    std.dl::library by_path = std.dl::open("/usr/lib/libSystem.B.dylib");
    std.dl::library by_name = std.dl::open("libSystem.B.dylib");
    std.dl::library copy = by_path;
    unsafe {
        raw void*? first = std.dl::symbol(&by_path, "strlen");
        raw void*? second = std.dl::symbol(&by_name, "strlen");
        raw void*? third = std.dl::symbol(&copy, "strlen");
        std.test::check(first != null, "strlen is defined");
        std.test::check(first == second, "both opens name the same library");
        std.test::check(first == third, "a copy names the same library");
        std.test::check(std.dl::symbol(&by_path, "r_no_such_symbol") == null,
                        "an undefined symbol has no address");
        std.test::check(std.dl::symbol(&by_path, "") == null, "an empty name has no address");
    }
}

@test
void refuses_paths() throws std.test::failure, std.alloc::alloc_error {
    try {
        std.dl::library missing = std.dl::open("/nonexistent/libnothing.dylib");
        missing as void;
        std.test::fail("a missing library opened");
    } catch (std.dl::dl_error failure) {
        std.test::check(failure.code == std.dl::error_code::not_loaded, "not_loaded");
    }
    try {
        std.dl::library empty = std.dl::open("");
        empty as void;
        std.test::fail("an empty path opened");
    } catch (std.dl::dl_error failure) {
        std.test::check(failure.code == std.dl::error_code::invalid_path, "invalid_path");
    }
    try {
        std.dl::library embedded = std.dl::open("libSystem.B.dylib\x00tail");
        embedded as void;
        std.test::fail("a path holding a NUL byte opened");
    } catch (std.dl::dl_error failure) {
        std.test::check(failure.code == std.dl::error_code::invalid_path, "invalid_path");
    }
}

@test
void resolves_a_loadable_import() throws std.test::failure, std.dl::dl_error, std.alloc::alloc_error {
    std.dl::library system = std.dl::open("libSystem.B.dylib");
    raw fn?(c_int) -> c_int absolute = std.dl::resolve(&system, abs);
    unsafe {
        std.test::check(absolute != null, "abs is defined");
        std.test::equal(absolute(-9i32 as c_int) as i32, 9i32);
    }
}
