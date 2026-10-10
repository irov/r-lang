module test.codegen.dynamic_loading;

import std.dl;

/* R-FFI-0063, Library R-SLIB-DL-0001..0004 (B9.3): functions of a library the program opens
   while it runs, verified against <stdlib.h> and <string.h> and obtained with std.dl::resolve. */
@link(name = "system.loader", kind = "loadable")
@header("stdlib.h")
@header("string.h")
extern "C" {
    @safety("LOADER-ABS", "value is not the most negative c_int")
    c_int abs(c_int value);

    @safety("LOADER-LABS", "value is not the most negative c_long")
    c_long labs(c_long value);

    @safety("LOADER-STRLEN", "text addresses a NUL-terminated string")
    c_size strlen(raw const c_char* text);
}

i32 main() {
    try {
        std.dl::library system = std.dl::open("/usr/lib/libSystem.B.dylib");
        raw fn?(c_int) -> c_int absolute = std.dl::resolve(&system, abs);
        raw fn?(c_long) -> c_long long_absolute = std.dl::resolve(&system, labs);
        unsafe {
            bool absolute_missing = absolute == null;
            bool long_missing = long_absolute == null;
            if (absolute_missing == true || long_missing == true) {
                return 1;
            }
            if (std.dl::symbol(&system, "r_no_such_symbol_in_the_library") != null) {
                return 2;
            }
            if (absolute(-42i32 as c_int) != 42i32 as c_int) {
                return 3;
            }
            if (long_absolute(-7i64 as c_long) != 7i64 as c_long) {
                return 4;
            }
        }
        std.dl::library again = std.dl::open("libSystem.B.dylib");
        unsafe {
            if (std.dl::symbol(&again, "strlen") == null) {
                return 5;
            }
        }
    } catch (std.dl::dl_error failure) {
        if (failure.code == std.dl::error_code::not_loaded) {
            return 10;
        }
        return 11;
    }
    try {
        std.dl::library missing = std.dl::open("/nonexistent/libnothing.dylib");
        missing as void;
        return 6;
    } catch (std.dl::dl_error failure) {
        if (failure.code != std.dl::error_code::not_loaded) {
            return 7;
        }
    }
    try {
        std.dl::library empty = std.dl::open("");
        empty as void;
        return 8;
    } catch (std.dl::dl_error failure) {
        if (failure.code != std.dl::error_code::invalid_path) {
            return 9;
        }
    }
    return 0;
}
