module test.codegen.ffi_bridge;

@link(name = "system.libc", kind = "system")
@header("stdlib.h")
@header("string.h")
extern "C" {
    @safety("ABS", "The function has no preconditions")
    c_int abs(c_int value);

    @safety("STRLEN", "text shall address a NUL-terminated string")
    c_size strlen(raw const c_char* text);
}

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-NAME", "The returned pointer addresses an immutable NUL-terminated string")
    raw const c_char* probe_name();
}

i32 main() {
    i32 outcome = 0;
    unsafe {
        c_int magnitude = abs(-42i32 as c_int);
        raw const c_char* name = probe_name();
        c_size length = strlen(name);
        if (magnitude != 42i32 as c_int) {
            outcome = 1;
        }
        if (length != 5usize as c_size) {
            outcome = 2;
        }
    }
    return outcome;
}
