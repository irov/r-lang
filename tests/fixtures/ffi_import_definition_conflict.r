module ffi.import_definition_conflict;

@link(name = "first", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);
}

@link(name = "second", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-NAME", "The returned pointer addresses an immutable NUL-terminated string")
    raw const c_char* probe_name();
}

i32 main() {
    unsafe {
        c_int value = probe_increment(1i32 as c_int);
        return value as i32 - 2;
    }
}
