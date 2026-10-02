module ffi.import_unknown_link;

@link(name = "absent", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);
}

i32 main() {
    unsafe {
        c_int value = probe_increment(1i32 as c_int);
        return value as i32 - 2;
    }
}
